
#include <node_api.h>
#include <assert.h>
#include "addon_api.h"
#include <stdlib.h>
#include <stdio.h>

// Limit ourselves to this many primes, starting at 2
#define PRIME_COUNT 10000
#define REPORT_EVERY 1000


// This function is responsible for converting data coming in from the worker
// thread to napi_value items that can be passed into JavaScript, and for
// calling the JavaScript function.
static void CallJs(napi_env env, napi_value js_cb, void *context, void *data)
{
  // This parameter is not used.
  (void)context;

  // Retrieve the prime from the item created by the worker thread.
  int the_prime = *(int *)data;

  // env and js_cb may both be NULL if Node.js is in its cleanup phase, and
  // items are left over from earlier thread-safe calls from the worker thread.
  // When env is NULL, we simply skip over the call into Javascript and free the
  // items.
  if (env != NULL)
  {
    napi_value undefined, js_the_prime;

    // Convert the integer to a napi_value.
    assert(napi_create_int32(env, the_prime, &js_the_prime) == napi_ok);

    // Retrieve the JavaScript `undefined` value so we can use it as the `this`
    // value of the JavaScript function call.
    assert(napi_get_undefined(env, &undefined) == napi_ok);

    // Call the JavaScript function and pass it the prime that the secondary
    // thread found.
    assert(napi_call_function(env,
                              undefined,
                              js_cb,
                              1,
                              &js_the_prime,
                              NULL) == napi_ok);
  }

  // Free the item created by the worker thread.
  free(data);
}

// This function runs on a worker thread. It has no access to the JavaScript
// environment except through the thread-safe function.
static void ExecuteWork(napi_env env, void *data)
{
  AddonData *addon_data = (AddonData *)data;
  int idx_inner, idx_outer;
  int prime_count = 0;

  // We bracket the use of the thread-safe function by this thread by a call to
  // napi_acquire_threadsafe_function() here, and by a call to
  // napi_release_threadsafe_function() immediately prior to thread exit.
  assert(napi_acquire_threadsafe_function(addon_data->tsfn) == napi_ok);

  // Find the first 1000 prime numbers using an extremely inefficient algorithm.
  for (idx_outer = 2; prime_count < PRIME_COUNT; idx_outer++)
  {
    for (idx_inner = 2; idx_inner < idx_outer; idx_inner++)
    {
      if (idx_outer % idx_inner == 0)
      {
        break;
      }
    }
    if (idx_inner < idx_outer)
    {
      continue;
    }

    // We found a prime. If it's the tenth since the last time we sent one to
    // JavaScript, send it to JavaScript.
    if (!(++prime_count % REPORT_EVERY))
    {

      // Save the prime number to the heap. The JavaScript marshaller (CallJs)
      // will free this item after having sent it to JavaScript.
      int *the_prime = (int *)malloc(sizeof(*the_prime));
      *the_prime = idx_outer;

      // Initiate the call into JavaScript. The call into JavaScript will not
      // have happened when this function returns, but it will be queued.
      assert(napi_call_threadsafe_function(addon_data->tsfn,
                                           the_prime,
                                           napi_tsfn_blocking) == napi_ok);
    }
  }

  // Indicate that this thread will make no further use of the thread-safe function.
  assert(napi_release_threadsafe_function(addon_data->tsfn,
                                          napi_tsfn_release) == napi_ok);
}

// This function runs on the main thread after `ExecuteWork` exits.
static void WorkComplete(napi_env env, napi_status status, void *data)
{
  AddonData *addon_data = (AddonData *)data;

  // Clean up the thread-safe function and the work item associated with this
  // run.
  assert(napi_release_threadsafe_function(addon_data->tsfn,
                                          napi_tsfn_release) == napi_ok);
  assert(napi_delete_async_work(env, addon_data->work) == napi_ok);

  // Set both values to NULL so JavaScript can order a new run of the thread.
  addon_data->work = NULL;
  addon_data->tsfn = NULL;
}

// Create a thread-safe function and an async queue work item. We pass the
// thread-safe function to the async queue work item so the latter might have a
// chance to call into JavaScript from the worker thread on which the
// ExecuteWork callback runs.
napi_value ThreadSafeAsyncStream(napi_env env, napi_callback_info info)
{
  size_t argc = 1;
  napi_value js_cb, work_name;
  AddonData *addon_data;

  // Retrieve the JavaScript callback we should call with items generated by the
  // worker thread, and the per-addon data.
  assert(napi_get_cb_info(env,
                          info,
                          &argc,
                          &js_cb,
                          NULL,
                          (void **)(&addon_data)) == napi_ok);

  // Ensure that no work is currently in progress.
  assert(addon_data->work == NULL && "Only one work item must exist at a time");

  // Create a string to describe this asynchronous operation.
  assert(napi_create_string_utf8(env,
                                 "N-API Thread-safe Call from Async Work Item",
                                 NAPI_AUTO_LENGTH,
                                 &work_name) == napi_ok);

  // Convert the callback retrieved from JavaScript into a thread-safe function
  // which we can call from a worker thread.
  assert(napi_create_threadsafe_function(env,
                                         js_cb,
                                         NULL,
                                         work_name,
                                         0,
                                         1,
                                         NULL,
                                         NULL,
                                         NULL,
                                         CallJs,
                                         &(addon_data->tsfn)) == napi_ok);

  // Create an async work item, passing in the addon data, which will give the
  // worker thread access to the above-created thread-safe function.
  assert(napi_create_async_work(env,
                                NULL,
                                work_name,
                                ExecuteWork,
                                WorkComplete,
                                addon_data,
                                &(addon_data->work)) == napi_ok);

  // Queue the work item for execution.
  assert(napi_queue_async_work(env, addon_data->work) == napi_ok);

  // This causes `undefined` to be returned to JavaScript.
  return NULL;
}


napi_value Init_ThreadSafeAsyncStream(napi_env env, napi_value exports)
{

  // Define addon-level data associated with this instance of the addon.
  AddonData *addon_data = (AddonData *)malloc(sizeof(*addon_data));
  addon_data->work = NULL;

  // Define the properties that will be set on exports.
  napi_property_descriptor start_work = {
      "ThreadSafeAsyncStream",
      NULL,
      ThreadSafeAsyncStream,
      NULL,
      NULL,
      NULL,
      napi_default,
      addon_data};

  // Decorate exports with the above-defined properties.
  assert(napi_define_properties(env, exports, 1, &start_work) == napi_ok);

  // Associate the addon data with the exports object, to make sure that when
  // the addon gets unloaded our data gets freed.
  assert(napi_wrap(env,
                   exports,
                   addon_data,
                   addon_getting_unloaded,
                   NULL,
                   NULL) == napi_ok);

  // Return the decorated exports object.
  return exports;
}
