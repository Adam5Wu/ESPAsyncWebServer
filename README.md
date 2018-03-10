# ESPAsyncWebServer
[![Build Status](https://travis-ci.org/Adam5Wu/ESPAsyncWebServer.svg?branch=feature/VFATFS)](https://travis-ci.org/Adam5Wu/ESPAsyncWebServer)
[![GitHub issues](https://img.shields.io/github/issues/Adam5Wu/ESPAsyncWebServer.svg)](https://github.com/Adam5Wu/ESPAsyncWebServer/issues)
[![GitHub forks](https://img.shields.io/github/forks/Adam5Wu/ESPAsyncWebServer.svg)](https://github.com/Adam5Wu/ESPAsyncWebServer/network)
[![License](https://img.shields.io/github/license/Adam5Wu/ESPAsyncWebServer.svg)](./LICENSE)

## Async HTTP and WebSocket Server for ESP8266 Arduino
This is a permenant fork of the [original project](https://github.com/me-no-dev/ESPAsyncWebServer).

While I preserved the major "async" taste of the original project, much of the core HTTP component has been almost completely re-written, for either improved performance, better protocol compatibility, or new features.

Handler-wise, this implementation should be compatible with the original project in terms of workflow. However, I may have some small adjustment on API parameters for better consistency and/or functionality improvements.

Compared with the original project, major new features are as the following:

### 1. Balanced multi-client serving
The original project uses un-arbitrated scheduling for request serving: when a request is being served, a small protion of the data is sent, and the TCP acknowledgement of the recepient of the data then triggers another portion of the data being sent, etc. As you may see, this forms a self-enchancing feedback loop.

The issue with this kind of scheduling is that, when multiple requests are in progress, it easily leads to starvation -- the connection that has slightly more share of the bandwidth tends to acquire even more bandwidth over time, while the others get less and less. The results is fluctaing and disproportional response time, e.g.:
- The same request sometimes got served in several milliseconds, but sometimes delayed for several seconds;
- Two response are served concurrently, one small (1KB) and one big (1MB), the small one can take longer to fulfill than the big one.
- Refer to the screenshot below, in "Un-arbitrated scheduling" column

In my implemention, I have applied a more controlled scheduling to balance bandwidth usage across concurrent requests, and achieves better "QoS" in multi-client serving scenarios:
- Requests serving time are much more consistent across time, with low or high workloads
- Responses are fulfilled with time proportional to their sizes, small transfer generally completes faster than big ones
- Refer to the screenshot below, in "Controlled scheduling" column

Requeust serving waterfall from Google Chrome:

| Un-arbitrated scheduling | Controlled scheduling |
| ------------------------ | --------------------- |
| <img src="docs/Async_NoSched.png"> | <img src="docs/Async_WithSched.png"> |

### 2. Improved digest authentication
ESP8266 is a fairly under-powered device, especially when it comes to memory. Serving multiple concurrent TLS sessions are infeasible in most scenarios. So we are practically stuck with unencrypted HTTP, and hence digest authentication becomes a necessity.

There are multiple extensions/flavors of digest authentication, MD5 is the most commonly implemented. My implemention extended the support to MD5-sess, which allows more efficient authentication.

In addition, support of SHA256(-sess) digest authentication will come in the near future -- brute forcing a MD5 collision is very feasible nowadays.

### 3. Fully offloaded authentication and access control
In conjunction of the [ESPEasyAuth](https://github.com/Adam5Wu/ESPEasyAuth), my implementation can completely offload authentication and access control from the handlers, so that develper only need to focus on handler functionality.

Authentication is handled by creating AccountAuthority and populate it with users and credentials (with a file, or programmatically), for example:
```
String Realm = "MyESP8266";
auto webAccounts = new HTTPDigestAccountAuthority(Realm);
File AccountData = fs.open("Account.txt", "r");
int AccountCnt = webAccounts->loadAccounts(AccountData);
webAccounts->addAccount("Admin", "Password");
```
`HTTPDigestAccountAuthority` uses hashed credentials for file-based storage, passwords will not appear in clear text in your flash.

Access control is handled by writing an ACL file or Stream (analogous to apache `.htaccess`), for example:
```
StreamString ACLData =
"/:GET:<Anonymous>\n"
"/api:GET,PUT:<Authenticated>\n"
"/config:PUT:Admin";
```

Load the two pieces of information into the Web server:
```
auto webAuthSessions = new SessionAuthority(webAccounts, webAccounts);
webServer->configAuthority(*webAuthSessions, ACLData);
```
Once the above steps are done, all authentication and access checks are **fully taken care of by the Web server, before request reaching the handlers**.
In other words, requests that reaches the handlers are guaranteed to be authenticated properly and have sufficient access, according to your account and ACL configurations.

### 4. WebDAV support
Work in progress

## Useful links
* Requires:
	- [ESP8266 Arduino Core Fork](https://github.com/Adam5Wu/Arduino-esp8266)
	- [ESPAsyncTCP](https://github.com/me-no-dev/ESPAsyncTCP)
	- [ZWUtils-Arduino](https://github.com/Adam5Wu/ZWUtils-Arduino)
* Optional:
	- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)
	- [ESPVFATFS](https://github.com/Adam5Wu/ESPVFATFS)
	- [ESPEasyAuth](https://github.com/Adam5Wu/ESPEasyAuth)
* Potentially interesting:
	- [esp8266FTPServer fork](https://github.com/Adam5Wu/esp8266FTPServer)

