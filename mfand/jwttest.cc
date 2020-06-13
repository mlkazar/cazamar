#include "jwt.h"
#include <stdio.h>
#include "json.h"

const char *main_examplep = "eyJ0eXAiOiJKV1QiLCJub25jZSI6Im5WOU9NZms2c0lVTHRlcWJCTlByVHBlRVZlVEZLVk85MU9ybXQ4T1pZaFUiLCJhbGciOiJSUzI1NiIsIng1dCI6IlNzWnNCTmhaY0YzUTlTNHRycFFCVEJ5TlJSSSIsImtpZCI6IlNzWnNCTmhaY0YzUTlTNHRycFFCVEJ5TlJSSSJ9.eyJhdWQiOiIwMDAwMDAwMy0wMDAwLTAwMDAtYzAwMC0wMDAwMDAwMDAwMDAiLCJpc3MiOiJodHRwczovL3N0cy53aW5kb3dzLm5ldC83MmY5ODhiZi04NmYxLTQxYWYtOTFhYi0yZDdjZDAxMWRiNDcvIiwiaWF0IjoxNTkyMDE1NjUzLCJuYmYiOjE1OTIwMTU2NTMsImV4cCI6MTU5MjAxOTU1MywiYWNjdCI6MCwiYWNyIjoiMSIsImFpbyI6IkFWUUFxLzhQQUFBQVh3YzJ0TWVoanBUYWVOT2Z1VkN1SU9aK3RDNjlBNFk1U2NzM2hwRm03d3VhVzA2MWZFbzRNZUgrR3VkUmRwSEJldG9XRlJ3c1RMNTdIRWYyd09tS2ZjYmRQRmNiZC90MUFlVmRqNWpJaVhNPSIsImFtciI6WyJwd2QiLCJtZmEiXSwiYXBwX2Rpc3BsYXluYW1lIjoiS2l0ZTRDbG91ZCIsImFwcGlkIjoiY2Q5N2NjZTctZjRhMy00MGMyLWFjYzctNjZhNzkyNGM2MzQxIiwiYXBwaWRhY3IiOiIxIiwiZGV2aWNlaWQiOiIyYTU0YWI4OC1lMGU4LTRiMTctODNlNC0zMjM1ZGZlZjI1YmEiLCJmYW1pbHlfbmFtZSI6IkthemFyIiwiZ2l2ZW5fbmFtZSI6Ik1pa2UiLCJpcGFkZHIiOiI3NC45OC4yMjAuMjI4IiwibmFtZSI6Ik1pa2UgS2F6YXIiLCJvaWQiOiJlMGZjYTNkMi04NTAzLTRkMDAtYjg5ZS04Y2VjOWViOWNiZjUiLCJvbnByZW1fc2lkIjoiUy0xLTUtMjEtMTI0NTI1MDk1LTcwODI1OTYzNy0xNTQzMTE5MDIxLTE4MDI4NDciLCJwbGF0ZiI6IjUiLCJwdWlkIjoiMTAwMzAwMDBBODREN0Y4QyIsInJoIjoiMC5BUm9BdjRqNWN2R0dyMEdScXkxODBCSGJSLWZNbDgyajlNSkFyTWRtcDVKTVkwRWFBSlkuIiwic2NwIjoiRmlsZXMuUmVhZFdyaXRlIHByb2ZpbGUgb3BlbmlkIGVtYWlsIiwic2lnbmluX3N0YXRlIjpbImR2Y19tbmdkIiwiZHZjX2NtcCJdLCJzdWIiOiJSelMyMHQ4ZVZlbklKRlh3Rk4wN0p3RVE5dG1NZjZuXzZ1cWM0MmU3aGJ3IiwidGVuYW50X3JlZ2lvbl9zY29wZSI6IldXIiwidGlkIjoiNzJmOTg4YmYtODZmMS00MWFmLTkxYWItMmQ3Y2QwMTFkYjQ3IiwidW5pcXVlX25hbWUiOiJtaWthemFyQG1pY3Jvc29mdC5jb20iLCJ1cG4iOiJtaWthemFyQG1pY3Jvc29mdC5jb20iLCJ1dGkiOiJfVnBCTE1UbkkwMnFiLWNBTzl3akFBIiwidmVyIjoiMS4wIiwieG1zX3N0Ijp7InN1YiI6InZFRjRROWFjVk9ObkpyQ2FUQkoyNXZ2aGZSclA2Q2dCWnpoLUlFb3hwUXMifSwieG1zX3RjZHQiOjEyODkyNDE1NDd9.rAp5WkvC3L0hu6yfyF7OimcZra1Yp19vQUx_f_k81DBfyytEyFuX88yUTfuoWhQ2UYEiI1VRIVkJvZqWvovy0kClenHhPuhEoqN76Kqyq6XJkp0VQGC25fXbZUvYKuSEbllia2SRpAXXwZbf1hapD860DYDirIu8IHxnNb6-FjSNhYmEjkmPjjivxfyBCGigkAgF8ScReBX3x9to31KNhdt5wk-u_qv9waPSr_wBgaR95iIH2k27grWqFI4kSUgy1kqzYSSzvan0Se0HKYyc2tpjyrbH5eAGDZFNRd40na0Gho7xw87eSqF9Zk_EcIqjwc2tUrpZinGRQGGj0rylOA";

int
main(int argc, char **argv)
{
    int32_t code;
    std::string result;
    char *jdatap;
    Json::Node *jnodep;
    Json::Node *childNodep;

    code = Jwt::decode(std::string(main_examplep), &result);
    
    printf("result code=%d\n", code);
    if (code == 0) {
        printf("%s\n", result.c_str());
    }

    Json jsys;
    jdatap = (char *) result.c_str();
    code = jsys.parseJsonChars(&jdatap, &jnodep);
    if (code == 0) {
        jnodep->print();
        childNodep = jnodep->searchForChild("unique_name", 0);
        if (childNodep) {
            printf("name is %s\n", childNodep->_children.head()->_name.c_str());
        }
    }
}
