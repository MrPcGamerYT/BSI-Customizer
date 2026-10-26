#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace GifRpcPayload {
inline std::string quote(const std::string &s) {
    std::string out="\"";
    const char hex[]="0123456789abcdef";
    for(unsigned char c:s) {
        if(c=='"'||c=='\\') {out+='\\';out+=char(c);}
        else if(c<32) {out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
        else out+=char(c);
    }
    return out+'"';
}
struct Config {
    bool enabled=false, timestamp=true;
    unsigned interval=5;
    std::string appId,state,largeImage,largeText,smallImage,smallText;
    std::vector<std::string> details;
    std::string label1,url1,label2,url2;
};
inline std::string activity(const Config &c, unsigned index, unsigned long pid, std::int64_t start, unsigned nonce) {
    std::string a="{\"state\":"+quote(c.state)+",\"details\":"+quote(c.details.empty()?"":c.details[index%c.details.size()]);
    if(c.timestamp) a+=",\"timestamps\":{\"start\":"+std::to_string(start)+"}";
    if(!c.largeImage.empty()||!c.smallImage.empty()) {
        a+=",\"assets\":{\"large_image\":"+quote(c.largeImage)+",\"large_text\":"+quote(c.largeText)
            +",\"small_image\":"+quote(c.smallImage)+",\"small_text\":"+quote(c.smallText)+"}";
    }
    std::string buttons;
    if(!c.label1.empty()&&!c.url1.empty()) buttons="{\"label\":"+quote(c.label1)+",\"url\":"+quote(c.url1)+"}";
    if(!c.label2.empty()&&!c.url2.empty()) {
        if(!buttons.empty()) buttons+=",";
        buttons+="{\"label\":"+quote(c.label2)+",\"url\":"+quote(c.url2)+"}";
    }
    if(!buttons.empty()) a+=",\"buttons\":["+buttons+"]";
    a+="}";
    return "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":"+std::to_string(pid)+",\"activity\":"+a+"},\"nonce\":"+quote(std::to_string(nonce))+"}";
}
}
