#include "../plugins/gif-rpc/RpcPayload.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace GifRpcPayload;
    assert(quote("a\"b\\c\n")=="\"a\\\"b\\\\c\\u000a\"");
    Config c;c.state="hello";c.details={"one","two"};
    auto a=activity(c,1,123,456,7);
    assert(a.find("\"details\":\"two\"")!=std::string::npos);
    assert(a.find("\"pid\":123")!=std::string::npos);
    c.timestamp=false;c.label1="Nova";c.url1="https://youtube.com/@nova_xis20";
    a=activity(c,0,123,456,8);
    assert(a.find("timestamps")==std::string::npos);
    assert(a.find("buttons")!=std::string::npos);
    Config empty;
    const auto minimal=activity(empty,0,123,456,9);
    assert(minimal.find("\"state\"")==std::string::npos);
    assert(minimal.find("\"details\"")==std::string::npos);
    assert(minimal.find("\"assets\"")==std::string::npos);
    std::cout<<"PASS: RPC payload escaping, rotation, timestamps and buttons\n";
}
