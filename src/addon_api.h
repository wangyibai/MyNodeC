
#ifndef _MYNC1_H_
#define _MYNC1_H_

#include <node_api.h>
#include <stdio.h>
#include "MyNativeObj.h"

napi_value MyC_SayHello(napi_env env, napi_callback_info info);
napi_value MyC_GetValueFromC (napi_env env, napi_callback_info info);
napi_value MyC_Print (napi_env env, napi_callback_info info);
napi_value SpeedTest_CPrimeCount (napi_env env, napi_callback_info info);
napi_value MyC_CreateJsonObject(napi_env env, const napi_callback_info info);

#endif  // _MYNC1_H_


