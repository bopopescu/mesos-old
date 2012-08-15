/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <jni.h>

#include <glog/logging.h>

#include <google/protobuf/io/zero_copy_stream_impl.h>

#include <string>
#include <map>

#include <mesos/mesos.hpp>

#include "construct.hpp"

using namespace mesos;

using std::map;
using std::string;


template <typename T>
T parse(const void* data, int size)
{
  // This should always get called with data that can be parsed (i.e.,
  // ParseFromZeroCopyStream should never return false) because we
  // have static type checking in Java and C++. A dynamic language
  // will not have this luxury.
  google::protobuf::io::ArrayInputStream stream(data, size);
  T t;
  bool parsed = t.ParseFromZeroCopyStream(&stream);
  CHECK(parsed) << "Unexpected failure while parsing protobuf";
  return t;
}


template <>
string construct(JNIEnv* env, jobject jobj)
{
  jstring js = (jstring) jobj;
  const char* s = env->GetStringUTFChars(js, NULL);
  CHECK(s != NULL) << "Out of memory!";
  string result(s);
  env->ReleaseStringUTFChars(js, s);
  return result;
}


template <>
map<string, string> construct(JNIEnv *env, jobject jobj)
{
  map<string, string> result;

  jclass clazz = env->GetObjectClass(jobj);

  // Set entrySet = map.entrySet();
  jmethodID entrySet = env->GetMethodID(clazz, "entrySet", "()Ljava/util/Set;");
  jobject jentrySet = env->CallObjectMethod(jobj, entrySet);

  clazz = env->GetObjectClass(jentrySet);

  // Iterator iterator = entrySet.iterator();
  jmethodID iterator = env->GetMethodID(clazz, "iterator", "()Ljava/util/Iterator;");
  jobject jiterator = env->CallObjectMethod(jentrySet, iterator);

  clazz = env->GetObjectClass(jiterator);

  // while (iterator.hasNext()) {
  jmethodID hasNext = env->GetMethodID(clazz, "hasNext", "()Z");

  jmethodID next = env->GetMethodID(clazz, "next", "()Ljava/lang/Object;");

  while (env->CallBooleanMethod(jiterator, hasNext)) {
    // Map.Entry entry = iterator.next();
    jobject jentry = env->CallObjectMethod(jiterator, next);

    clazz = env->GetObjectClass(jentry);

    // String key = entry.getKey();
    jmethodID getKey = env->GetMethodID(clazz, "getKey", "()Ljava/lang/Object;");
    jobject jkey = env->CallObjectMethod(jentry, getKey);

    // String value = entry.getValue();
    jmethodID getValue = env->GetMethodID(clazz, "getValue", "()Ljava/lang/Object;");
    jobject jvalue = env->CallObjectMethod(jentry, getValue);

    const string& key = construct<string>(env, jkey);
    const string& value = construct<string>(env, jvalue);

    result[key] = value;
  }

  return result;
}


#define PROTOBUF_CONSTRUCT(Type) \
  template <> \
  Type construct(JNIEnv* env, jobject jobj) { \
    jclass clazz = env->GetObjectClass(jobj); \
    jmethodID toByteArray = env->GetMethodID(clazz, "toByteArray", "()[B"); \
    jbyteArray jdata = (jbyteArray) env->CallObjectMethod(jobj, toByteArray); \
    jbyte* data = env->GetByteArrayElements(jdata, NULL); \
    jsize length = env->GetArrayLength(jdata); \
    const Type& result = parse<Type>(data, length); \
    env->ReleaseByteArrayElements(jdata, data, 0); \
    return result; \
  } \
  class DummyClassForSemicolon

#define PROTOBUF_ENUM_CONSTRUCT(Type) \
  template <> \
  TaskState construct(JNIEnv* env, jobject jobj) \
  { \
    jclass clazz = env->FindClass("org/apache/mesos/Protos$" # Type); \
    jmethodID getNumber = env->GetStaticMethodID(clazz, "getNumber", "()I"); \
    jint jvalue = env->CallIntMethod(jobj, getNumber); \
    return (Type) jvalue; \
  } \
  class DummyClassForSemicolon

PROTOBUF_CONSTRUCT(FrameworkInfo);
PROTOBUF_CONSTRUCT(Filters);
PROTOBUF_CONSTRUCT(FrameworkID);
PROTOBUF_CONSTRUCT(ExecutorID);
PROTOBUF_CONSTRUCT(TaskID);
PROTOBUF_CONSTRUCT(SlaveID);
PROTOBUF_CONSTRUCT(OfferID);
PROTOBUF_ENUM_CONSTRUCT(TaskState);
PROTOBUF_CONSTRUCT(TaskInfo);
PROTOBUF_CONSTRUCT(TaskStatus);
PROTOBUF_CONSTRUCT(ExecutorInfo);
PROTOBUF_CONSTRUCT(Request);
PROTOBUF_CONSTRUCT(Progress);
