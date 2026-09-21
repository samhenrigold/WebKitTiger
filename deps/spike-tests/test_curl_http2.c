#include <curl/curl.h>
#include <stdio.h>
static size_t discard(void *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr; (void)userdata; return size * nmemb;
}
int main(int argc, char **argv) {
    const char *cainfo = argc > 1 ? argv[1] : "/tmp/cacert.pem";
    printf("%s\n", curl_version());
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, "https://www.apple.com/");
    curl_easy_setopt(c, CURLOPT_CAINFO, cainfo);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    long httpver = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(c, CURLINFO_HTTP_VERSION, &httpver);
    if (rc != CURLE_OK) {
        printf("curl_easy_perform failed: %s (%d)\n", curl_easy_strerror(rc), rc);
    } else {
        const char *verstr = httpver == CURL_HTTP_VERSION_2_0 ? "HTTP/2" :
                              httpver == CURL_HTTP_VERSION_1_1 ? "HTTP/1.1" :
                              httpver == CURL_HTTP_VERSION_1_0 ? "HTTP/1.0" : "unknown";
        printf("HTTPS GET status: %ld negotiated: %s (%ld)\n", status, verstr, httpver);
    }
    curl_easy_cleanup(c);
    curl_global_cleanup();
    return rc != CURLE_OK;
}
