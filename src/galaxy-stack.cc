/**
 * Copyright (c) 2013 Bruno Jouhier <bruno.jouhier@sage.com>
 * MIT License
 */
#define ENABLE_DEBUGGER_SUPPORT
#include "api.h"
#include "objects.h"
#include "vm-state-inl.h"
#include <node.h>
#include <v8.h>

using namespace v8;

// BEGIN CODE COPIED FROM api.cc
#define ENTER_V8(isolate)                                          \
  ASSERT((isolate)->IsInitialized());                              \
  i::VMState<i::OTHER> __state__((isolate))

#define ON_BAILOUT(isolate, location, code)                        \
  if (IsDeadCheck(isolate, location) ||                            \
      IsExecutionTerminatingCheck(isolate)) {                      \
    code;                                                          \
    UNREACHABLE();                                                 \
  }

static void DefaultFatalErrorHandler(const char* location,
                                     const char* message) {
  i::Isolate* isolate = i::Isolate::Current();
  if (isolate->IsInitialized()) {
    i::VMState<i::OTHER> state(isolate);
    API_Fatal(location, message);
  } else {
    API_Fatal(location, message);
  }
}

static FatalErrorCallback GetFatalErrorHandler() {
  i::Isolate* isolate = i::Isolate::Current();
  if (isolate->exception_behavior() == NULL) {
    isolate->set_exception_behavior(DefaultFatalErrorHandler);
  }
  return isolate->exception_behavior();
}

static bool ReportV8Dead(const char* location) {
  FatalErrorCallback callback = GetFatalErrorHandler();
  callback(location, "V8 is no longer usable");
  return true;
}

static inline bool IsDeadCheck(i::Isolate* isolate, const char* location) {
  return !isolate->IsInitialized()
      && i::V8::IsDead() ? ReportV8Dead(location) : false;
}


static inline bool IsExecutionTerminatingCheck(i::Isolate* isolate) {
  if (!isolate->IsInitialized()) return false;
  if (isolate->has_scheduled_exception()) {
    return isolate->scheduled_exception() ==
        isolate->heap()->termination_exception();
  }
  return false;
}
// END CODE COPIED FROM api.cc

// Got this the API boilerplate from v8::Object::GetPrototype() 
// and the details from Isolate::CaptureCurrentStackTrace
Local<Value> internalGetStackFrame(Handle<Value> handle) {
  i::Isolate* isolate = i::Isolate::Current();
  ON_BAILOUT(isolate, "Galaxy_stack::GetStackFrame()", return Local<v8::Value>());
  ENTER_V8(isolate);
  i::Handle<i::JSGeneratorObject> gen = Utils::OpenHandle(*handle);
  i::Handle<i::JSFunction> fun(gen->function(), isolate);
  i::Handle<i::Script> script(i::Script::cast(fun->shared()->script()));
  i::Address pc = gen->function()->code()->instruction_start();
  int script_line_offset = script->line_offset()->value();
  int position = fun->code()->SourcePosition(pc + gen->continuation());
  int line_number = GetScriptLineNumber(script, position);
  // line_number is already shifted by the script_line_offset.
  int relative_line_number = line_number - script_line_offset;

  i::Handle<i::FixedArray> line_ends(i::FixedArray::cast(script->line_ends()));
  int start = (relative_line_number == 0) ? 0 : i::Smi::cast(line_ends->get(relative_line_number - 1))->value() + 1;
  int column_offset = position - start;
  if (relative_line_number == 0) {
    // For the case where the code is on the same line as the script
    // tag.
    column_offset += script->column_offset()->value();
  }
  i::Handle<i::Object> script_name(script->name(), isolate);
  i::Handle<i::Object> fun_name(fun->shared()->name(), isolate);
  if (!fun_name->BooleanValue()) {
    fun_name = i::Handle<i::Object>(fun->shared()->inferred_name(), isolate);
  }

  i::Handle<i::JSObject> stack_frame = isolate->factory()->NewJSObject(isolate->object_function());
  i::Handle<i::String> column_key = isolate->factory()->InternalizeOneByteString(STATIC_ASCII_VECTOR("column"));
  i::Handle<i::String> line_key = isolate->factory()->InternalizeOneByteString(STATIC_ASCII_VECTOR("lineNumber"));
  i::Handle<i::String> script_key = isolate->factory()->InternalizeOneByteString(STATIC_ASCII_VECTOR("scriptName"));  
  i::Handle<i::String> function_key = isolate->factory()->InternalizeOneByteString(STATIC_ASCII_VECTOR("functionName"));
  i::JSObject::SetLocalPropertyIgnoreAttributes(stack_frame, script_key, script_name, NONE);
  i::JSObject::SetLocalPropertyIgnoreAttributes(stack_frame, line_key, i::Handle<i::Smi>(i::Smi::FromInt(line_number + 1), isolate), NONE); 
  i::JSObject::SetLocalPropertyIgnoreAttributes(stack_frame, column_key, i::Handle<i::Smi>(i::Smi::FromInt(column_offset + 1), isolate), NONE);
  i::JSObject::SetLocalPropertyIgnoreAttributes(stack_frame, function_key, fun_name, NONE);
  //printf("script name: %s\n", *v8::String::Utf8Value(script_name->toString());
  return Utils::ToLocal(stack_frame); 
}

Handle<Value> GetStackFrame(const Arguments& args) {
  HandleScope scope;

  if (args.Length() != 1) {
    ThrowException(Exception::TypeError(String::New("Wrong number of arguments")));
    return scope.Close(Undefined());
  }

  if (!args[0]->IsObject()) {
    ThrowException(Exception::TypeError(String::New("Wrong argument type")));
    return scope.Close(Undefined());
  }
  Local<Value> result = internalGetStackFrame(args[0]);
  return scope.Close(result);

}

void init(Handle<Object> exports) {
  exports->Set(String::NewSymbol("getStackFrame"), FunctionTemplate::New(GetStackFrame)->GetFunction());
}

NODE_MODULE(galaxy_stack, init)