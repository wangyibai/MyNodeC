const myaddon = require('bindings')('mync1');

// Call the function "ThreadSafeAsyncStream" which the native bindings library exposes.
// The function accepts a callback which it will call from the worker thread and
// into which it will pass prime numbers. This callback simply prints them out.
function TestThreadSafeAsyncStream() {

  myaddon.ThreadSafeAsyncStream(  (thePrime) =>
    console.log("Received prime from secondary thread: " + thePrime));

}

function Main() {
      TestThreadSafeAsyncStream();
}
Main();
