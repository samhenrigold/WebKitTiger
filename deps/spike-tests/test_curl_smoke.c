/* x86_64 curl smoke test against real sites: TLS handshake protocol (via a filtered
 * CURLOPT_DEBUGFUNCTION trace line), negotiated HTTP version, negotiated content encoding,
 * and time-to-first-byte. Usage: test_curl_smoke <url> <cainfo> [min-tls] [max-tls]
 * min/max-tls: "1.2" or "1.3" (both default to auto/highest if omitted). */
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

static char g_tls_line[256] = "";

static int debug_cb(CURL *h, curl_infotype type, char *data, size_t size, void *userp) {
    (void)h; (void)userp;
    if (type == CURLINFO_TEXT && g_tls_line[0] == '\0') {
        /* curl's verbose trace prints a line like "* TLSv1.3 (IN), TLS handshake, ..." or
         * "* SSL connection using TLSv1.3 / ..." right after the handshake completes. */
        if (memmem(data, size, "SSL connection using", 20)) {
            size_t n = size < sizeof(g_tls_line) - 1 ? size : sizeof(g_tls_line) - 1;
            memcpy(g_tls_line, data, n);
            g_tls_line[n] = 0;
            char *nl = strchr(g_tls_line, '\n');
            if (nl) *nl = 0;
        }
    }
    return 0;
}

static char g_content_encoding[64] = "(none)";

static size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
    (void)userdata;
    size_t n = size * nitems;
    if (n > 19 && strncasecmp(buffer, "Content-Encoding:", 17) == 0) {
        size_t len = n - 18 < sizeof(g_content_encoding) - 1 ? n - 18 : sizeof(g_content_encoding) - 1;
        memcpy(g_content_encoding, buffer + 18, len);
        g_content_encoding[len] = 0;
        char *cr = strpbrk(g_content_encoding, "\r\n");
        if (cr) *cr = 0;
    }
    return n;
}

static size_t discard(void *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr; (void)userdata; return size * nmemb;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <url> <cainfo> [minmax-tls: 1.2|1.3|auto]\n", argv[0]); return 2; }
    const char *url = argv[1];
    const char *cainfo = argv[2];
    const char *tlsver = argc > 3 ? argv[3] : "auto";

    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_CAINFO, cainfo);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 25L);
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");  /* advertise/accept all built-in encodings */
    curl_easy_setopt(c, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(c, CURLOPT_DEBUGFUNCTION, debug_cb);
    if (strcmp(tlsver, "1.2") == 0) {
        curl_easy_setopt(c, CURLOPT_SSLVERSION, (long)(CURL_SSLVERSION_TLSv1_2 | CURL_SSLVERSION_MAX_TLSv1_2));
    } else if (strcmp(tlsver, "1.3") == 0) {
        curl_easy_setopt(c, CURLOPT_SSLVERSION, (long)(CURL_SSLVERSION_TLSv1_3 | CURL_SSLVERSION_MAX_DEFAULT));
    }

    CURLcode rc = curl_easy_perform(c);
    long status = 0, httpver = 0;
    double ttfb = 0, total = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(c, CURLINFO_HTTP_VERSION, &httpver);
    curl_easy_getinfo(c, CURLINFO_STARTTRANSFER_TIME, &ttfb);
    curl_easy_getinfo(c, CURLINFO_TOTAL_TIME, &total);

    if (rc != CURLE_OK) {
        printf("%s: FAILED: %s (%d)\n", url, curl_easy_strerror(rc), rc);
        curl_easy_cleanup(c); curl_global_cleanup();
        return 1;
    }
    const char *verstr = httpver == CURL_HTTP_VERSION_2_0 ? "HTTP/2" :
                          httpver == CURL_HTTP_VERSION_1_1 ? "HTTP/1.1" :
                          httpver == CURL_HTTP_VERSION_1_0 ? "HTTP/1.0" : "?";
    printf("%s [tls-req=%s]: status=%ld http=%s encoding=%s ttfb=%.3fs total=%.3fs\n  %s\n",
           url, tlsver, status, verstr, g_content_encoding, ttfb, total,
           g_tls_line[0] ? g_tls_line : "(no TLS trace line captured)");

    curl_easy_cleanup(c);
    curl_global_cleanup();
    return 0;
}
