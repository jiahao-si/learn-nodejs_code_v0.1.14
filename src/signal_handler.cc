// Copyright 2009 Ryan Dahl <ry@tinyclouds.org>
#include <signal_handler.h>
#include <assert.h>

namespace node {

using namespace v8;

Persistent<FunctionTemplate> SignalHandler::constructor_template;

void SignalHandler::Initialize(Handle<Object> target) {
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(SignalHandler::New);
  constructor_template = Persistent<FunctionTemplate>::New(t);
  constructor_template->Inherit(EventEmitter::constructor_template);
  constructor_template->InstanceTemplate()->SetInternalFieldCount(1);
  constructor_template->SetClassName(String::NewSymbol("SignalHandler"));

  NODE_SET_PROTOTYPE_METHOD(constructor_template, "stop", SignalHandler::Stop);

  target->Set(String::NewSymbol("SignalHandler"),
      constructor_template->GetFunction());
}

void SignalHandler::OnSignal(EV_P_ ev_signal *watcher, int revents) {
  SignalHandler *handler = static_cast<SignalHandler*>(watcher->data);
  HandleScope scope;

  assert(revents == EV_SIGNAL);

  handler->Emit("signal", 0, NULL);
}

SignalHandler::~SignalHandler() {
  if (watcher_.active) {
    ev_unref(EV_DEFAULT_UC);
    ev_signal_stop(EV_DEFAULT_UC_ &watcher_);
  }
}

Handle<Value> SignalHandler::New(const Arguments& args) {
  HandleScope scope;

  if (args.Length() != 1 || !args[0]->IsInt32()) {
    return ThrowException(String::New("Bad arguments"));
  }

  int sig = args[0]->Int32Value();

  SignalHandler *handler = new SignalHandler();
  handler->Wrap(args.Holder());

  ev_signal_init(&handler->watcher_, SignalHandler::OnSignal, sig);
  handler->watcher_.data = handler;
  ev_signal_start(EV_DEFAULT_UC_ &handler->watcher_);
  ev_unref(EV_DEFAULT_UC);

  handler->Attach();

  return args.This();
}

Handle<Value> SignalHandler::Stop(const Arguments& args) {
  HandleScope scope;

  SignalHandler *handler = ObjectWrap::Unwrap<SignalHandler>(args.Holder());
  ev_ref(EV_DEFAULT_UC);
  ev_signal_stop(EV_DEFAULT_UC_ &handler->watcher_);
  handler->Detach();
  return Undefined();
}

}  // namespace node
