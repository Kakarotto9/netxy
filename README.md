netxy
=======
使用C ++ 11的跨平台高性能TCP网络库和RPC库

## Features
* 跨平台（Linux | Windows）
* 高性能和安全使用。
* 没有依赖
* 多线程
* SSL支持
* 支持HTTP，HTTPS，WebSocket协议
* IPv6支持
* RPC库


## Benchamrk
  在localhost下，使用CentOS 6.5虚拟主机（主机为Win10 i5）

* PingPong

  Benchamrk的服务器和客户端都只使用一个线程，数据包大小为4k

  ![PingPong](image/pingpong.png "PingPong")

* Broadcast

  服务器使用两个网络线程和一个逻辑线程，客户端使用一个网络（也称为进程逻辑）线程。每个数据包大小为46个字节。

  每个数据包都包含客户端的ID。服务器在从任何客户端收集一个数据包时向所有客户端广播数据包。

  当来自服务器的recv数据包和数据包的id等于self时，客户端发送一个数据包

  ![Broadcast](image/broadcast.png "Broadcast")

* Ab HTTP(1 network thread)
        Document Path:          /
        Document Length:        18 bytes

        Concurrency Level:      100
        Time taken for tests:   5.871 seconds
        Complete requests:      100000
        Failed requests:        0
        Write errors:           0
        Non-2xx responses:      100000
        Total transferred:      5200000 bytes
        HTML transferred:       1800000 bytes
        Requests per second:    17031.62 [#/sec] (mean)
        Time per request:       5.871 [ms] (mean)
        Time per request:       0.059 [ms] (mean, across all concurrent requests)
        Transfer rate:          864.89 [Kbytes/sec] received

        Connection Times (ms)
                    min  mean[+/-sd] median   max
        Connect:        0    2   0.7      2       8
        Processing:     1    3   0.7      3       9
        Waiting:        0    3   0.8      3       8
        Total:          2    6   0.8      6      11

        Percentage of the requests served within a certain time (ms)
        50%      6
        66%      6
        75%      6
        80%      6
        90%      7
        95%      7
        98%      7
        99%      8
        100%     11 (longest request)

## About session safety
该库使用三层标识一个会话（也是使用此库的三种方式）。

* 使用名为DataSocket的原始指针，与使用的第一层包装器DataSocket中的EventLoop结合使用

* 使用int64_t number ident一个会话，用于TCPService的一些回调，包装第一层的DataSocket

* 使用名为TCPSession :: PTR的智能指针与WrapServer结合，可以通过TCPSession :: PTR控制会话

我建议你使用上面的第二种或第三种方式，因为不要担心内存管理器

## About RPC

使用这个RPC库，你不需要任何proto文件作为Protobuf和Thrift，因为我使用C ++模板通用编程做这项工作。

RPC支持任何C ++基类型，sush为int，string，vector，map和支持Protobuf消息类型; 当然，RPC可以使用异步回调模式，当你需要从服务器返回进程RPC回复msg时。

在服务器端：

```cpp
static int add(int a, int b)
{
    return a + b;
}

static void addNoneRet(int a, int b, netxy::rpc::RpcRequestInfo reqInfo)
{
    // send reply when other async call completed
    /*
        auto caller = RpcServer::getRpcFromer();
        redis->get("k", [caller, reqInfo](const std::string& value){
            caller->reply(reqInfo, value);
        });
    */
}

void registerServices()
{
    RpcServer::def("add", add);
    RpcServer::def("add", addNoneRet);
}
```

在客户端:

```cpp
rpcClient->call("add", 1, 2);
rpcClient->call("add", 1, 2, [](int result) {
    cout << result << endl;
});
```

Examples
----------------------------
* [PingPongServer](https://github.com/Kakarotto9/netxy/blob/master/examples/PingPongServer.cpp)
* [PingPongClient](https://github.com/Kakarotto9/netxy/blob/master/examples/PingPongClient.cpp)
* [BroadCastServer](https://github.com/Kakarotto9/netxy/blob/master/examples/BroadCastServer.cpp)
* [BroadCastClient](https://github.com/Kakarotto9/netxy/blob/master/examples/BroadCastClient.cpp)
* [SimpleHttpServer](https://github.com/Kakarotto9/netxy/blob/master/examples/TestHttp.cpp) 显示如何启动http服务并请求http
* [BenchWebsocket](https://github.com/Kakarotto9/netxy/blob/master/examples/BenchWebsocket.cpp) 基准测试websocket服务器
* [WebSocketProxy](https://github.com/Kakarotto9/netxy/blob/master/examples/WebBinaryProxy.cpp) websocket客户端和二进制协议服务器之间的一个代理服务器
* [SimpleRpcServer](https://github.com/Kakarotto9/netxy/blob/master/examples/SimpleRpcServer.cpp) rpc服务器使用http和protobuf
* more examples please see [examples](https://github.com/Kakarotto9/netxy/tree/master/examples);
