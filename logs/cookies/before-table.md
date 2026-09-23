| case | verdict | detail |
| --- | --- | --- |
| document.cookie shows non-HttpOnly (incl. ct0) | FAIL | missing __Host-ok __Secure-ok c_del c_domain c_exp2039 c_expires c_lax c_long c_maxage c_plain c_secure c_sn c_strict ct0 |
| __Host-/__Secure- prefix rules | FAIL | missing __Host-ok __Secure-ok |
| long value (3000 bytes) | FAIL | missing c_long |
| Expires in 2031 and 2039 | FAIL | missing c_exp2039 c_expires |
| fetch(credentials) sends all, incl. HttpOnly | FAIL | missing __Host-ok __Secure-ok auth_token c_domain c_exp2039 c_expires c_httponly c_js c_jsp c_lax c_long c_maxage c_plain c_secure c_sn c_strict c_xhr ct0 |
| JS cannot overwrite HttpOnly | FAIL | c_httponly=None |
| Set-Cookie on fetch response; Max-Age=0 deletes | FAIL | missing c_js c_jsp c_xhr |
| Path=/sub sent under /sub only | FAIL | missing c_path |
| 302 chain sets cookies on each hop | FAIL | missing __Host-ok __Secure-ok auth_token c_domain c_exp2039 c_expires c_httponly c_js c_jsp c_lax c_long c_maxage c_plain c_r1 c_r2 c_secure c_sn c_strict c_xhr ct0 |
| http: no Secure cookies sent | FAIL | missing c_domain c_exp2039 c_expires c_httponly c_js c_jsp c_lax c_long c_maxage c_plain c_r1 c_r2 c_strict |
| http: Set-Cookie with Secure refused | PASS | ok |
| cross-site top level: A's cookies stay on A | PASS | ok |
| third-party iframe cookie (Safari blocks) | PASS | ok |
| cross-site fetch: Lax/Strict withheld | PASS | ok |
| cross-site nav back: Strict withheld | PASS | ok |
| back on A after B: session cookies persist | FAIL | missing __Host-ok __Secure-ok auth_token c_domain c_exp2039 c_expires c_httponly c_js c_jsp c_lax c_long c_maxage c_plain c_r1 c_r2 c_secure c_sn c_xhr ct0 |
| relaunch: persistent survive | FAIL | missing auth_token c_exp2039 c_expires c_jsp c_maxage ct0 |
| relaunch: session cookies gone | PASS | ok |

6/18 not failing
