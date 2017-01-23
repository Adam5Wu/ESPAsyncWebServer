# Modifications to original project

## File Request Handling

- ``AsyncStaticWebHandler`` mostly reimplemented in a file system independent fashion.

	This enables painless hook up with VFATFS or future file systems that conforms to or extends the Arduino FS interface.
- Adopt extended FS interface for retrieving modification time and other attributes

	This enables conventional modification-based cache control to work properly.

	Note that currently implementation uses weak E-Tag based on last modification time and file size. This avoids relatively more expensive data time formatting, and should achieve similar effect, just a little less semantics when you debug HTTP header.

## Response Serving

- A corner case problem is discovered in the original project, when concurrently serving multiple files, memory resource contention can lead to silent partial loss of response data, most likely at the beginning part.
	- This leads me to try improving the serving procedure, which eventually, became a complete rewrite of the WebResponses classes.

- The new implementation features:
	- A more robust data serving logic, does not silently lose data under memory contention
	- A slightly improved class abstraction, which conserves a bit of memory when serving simply responses (such as status code only response)
	- In-place response header generation without string formatting, conserve heap usage and processor cycles

- Improved debug and regular logging:
	- Two level debug logging, all message contains remote identifiers (IP:port) to help diagnosis
	- Regularly logs each request's IP:port, method, host, url and response code

- Proactive memory congestion detection and throttling is implemented, which can avoid random crashes due to heap overflow at high workloads

## Request Parsing

- A small modification is done on `AsyncWebServerRequest::_onData()` method, converting the code logic from recursive to iterative.

	While I debug some crash dumps during development, I often observe an amazingly long (10+ hops) chain of `_onData()` calls on the stack trace. This modification should reduce the stack usage and eliminate those long chains.

- Parser efficicy improvements, avoiding redundant string copying by utilizing string move constructions and in-place truncations.

## API changes

- I have slightly adjusted the parameter orders of some `AsyncWebServerRequest::beginResponse()` and `AsyncWebServerRequest::send()` APIs, in an attempt to provide some unification of parameter orders across all these APIs.