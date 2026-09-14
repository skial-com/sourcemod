/* OpenSSL 3 removed the exported SSL_get_peer_certificate symbol (the old name
 * survives only as a header macro aliasing SSL_get1_peer_certificate). The
 * prebuilt MariaDB Connector/C we statically link was built against OpenSSL 1.1
 * and still references the old symbol, so it is undefined at load time when the
 * host provides OpenSSL 3 (e.g. "undefined symbol: SSL_get_peer_certificate").
 *
 * Provide it as a WEAK alias to SSL_get1_peer_certificate. On OpenSSL 3 this is
 * the only definition, so the connector resolves against it; on OpenSSL 1.1 the
 * library's own strong symbol takes precedence and this is ignored. Opaque
 * forward declarations keep it independent of which OpenSSL headers are present.
 * Semantics match: both return a certificate with an incremented refcount that
 * the caller frees, which is what the connector expects. */
typedef struct ssl_st SSL;
typedef struct x509_st X509;
extern X509 *SSL_get1_peer_certificate(const SSL *ssl);

__attribute__((weak)) X509 *SSL_get_peer_certificate(const SSL *ssl) {
    return SSL_get1_peer_certificate(ssl);
}
