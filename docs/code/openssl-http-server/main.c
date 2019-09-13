
/**
 * a simple http server using libuv,openssl and htpp_parser.
 * it is based openssl mem BIO as follow (copied from
 * https://github.com/darrenjs/openssl_examples):
 *
 *
 * +------+                                    +-----+
 * |......|--> read(fd) --> BIO_write(rbio) -->|.....|--> SSL_read(ssl)  --> IN
 * |......|                                    |.....|
 * |.sock.|                                    |.SSL.|
 * |......|                                    |.....|
 * |......|<-- write(fd) <-- BIO_read(wbio) <--|.....|<-- SSL_write(ssl) <-- OUT
 * +------+                                    +-----+
 *
 *        |                                  |       |                     |
 *        |<-------------------------------->|       |<------------------->|
 *        |         encrypted bytes          |       |  unencrypted bytes  |
 * at any point, SSL engin may produce some data and write it to out going BIO
 * and report a SSL_ERROR_WANT_READ but actually it must flush out going BIO.
 * this is because engine writes its produced data to a MEM BIO that is allways
 * async and returns OK. so after any SSL operation flush the outgoing BIO.
 *
 * As this operation is so ambiguous to implement; this file is full of
 * explaination.
 */

#include <openssl/bio.h> /** < for BIO types and functions */
#include <openssl/err.h> /** < for Error types and functions */
#include <openssl/pem.h> /** < for reading cert. and key */
#include <openssl/ssl.h> /** < for SSL types and functions */
#include <uv.h>          /** < main respectful file :) */

#include <http_parser.h> /** < parsing HTTP messages */

#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#define CERT "cert.pem"
#define KEY "key.pem"

uv_loop_t *loop; /** < the main loop. it is declared globally to be accessible
        from any point */

SSL_CTX *sslContext; /** < only one object of this type is requerd per server */

const SSL_METHOD *sslMethod; /** < it specifies the protocol corresponding to
                                any individual SSL/TLS version */

/**
 * @brief Initalizes the SSL objects like `sslMethod` and `sslContext`
 *
 * @param cert_file name of certificate file
 * @param key_file name of private key file
 */
void init_ssl(const char *cert_file, const char *key_file) {
  SSL_library_init();           /** < initalize the ssl library*/
  OpenSSL_add_all_algorithms(); /** < add ALL algorithms available in SSL
                                   engine*/
  SSL_load_error_strings();  /** < load error strings to be able report errors
                                using this strings*/
  ERR_load_BIO_strings();    /** < load error string for BIO library*/
  ERR_load_crypto_strings(); /** < load error string for crypto library*/

  sslMethod = TLSv1_2_method(); /** < use TLS 1.2 */
  sslContext =
      SSL_CTX_new(sslMethod); /** < create a SSL context based on methods
                                 required to implement TLS 1.2*/

  SSL_CTX_set_verify(sslContext, SSL_VERIFY_NONE,
                     NULL); /** only verify SSL certificate, not client one */

  SSL_CTX_use_certificate_file(sslContext, cert_file, SSL_FILETYPE_PEM) == 1
      ? printf("+ SSL CERT LOAD OK\n")
      : printf("- SSL CERT LOAD ERROR\n"); /** load certificate */

  SSL_CTX_use_PrivateKey_file(sslContext, key_file, SSL_FILETYPE_PEM) == 1
      ? printf("+ SSL KEY LOAD OK\n")
      : printf("- SSL KEY LOAD ERROR\n"); /** load private key */
}

// struct to hold header of http protocol
typedef struct header {
  char *field;
  char *value;
  struct header *next;
} header;

// a single http request
typedef struct {
  char *method;
  char *url;
  header *headers;
  size_t headers_size;
  header *end;
  char *body;
  size_t body_lenght;
} request;

// read the http_parser doc for it
enum parsing_stat { FIELD = 0x0, VALUE = 0x1 };

// http sesson
typedef struct {
  uv_tcp_t connection; /** < any session have a handle */
  request rqst;        /** < request sent by client */
  http_parser parser;  /** < HTTP parser needed to parse messages */
  http_parser_settings
      parser_settings; /** < parser_settings holds callbackes called on any http
                          message get parse */
  enum parsing_stat parsing_status; /** < we ar parsing a field name or field
                                       value? (see http_parser doc) */

  SSL *ssl;  /** < any ssl sesson must have one */
  BIO *rbio; /** < this BIO is for reading encrypted data */
  BIO *wbio; /** < this BIO is for writing encrypted data */

} session;

// uv write request as allways (like tcp-echo-server)
typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

void free_write_req(uv_write_t *req) {
  write_req_t *wr = (write_req_t *)req;
  free(wr->buf.base);
  free(wr);
}

void write_cb(uv_write_t *req, int status) { free_write_req(req); }

void flush_write_bio(BIO *wbio, uv_tcp_t *handle) {
  write_req_t *req = (write_req_t *)malloc(sizeof(write_req_t));

  req->buf.base = (char *)malloc(BIO_pending(wbio));
  req->buf.len = BIO_pending(wbio);
  BIO_read(wbio, req->buf.base, req->buf.len);

  uv_write((uv_write_t *)req, (uv_stream_t *)handle, &req->buf, 1, write_cb);
}

/**
 * HTTP_Parser functions and callbacks
 */

/**
 * @brief called by parser when http message begins
 *
 * @param parser the parser object
 * @return int
 */
int on_message_begin(http_parser *parser) {
  session *ses = (session *)parser->data;
  memset(&ses->rqst, 0, sizeof(request)); /** < resete session's request object
                                             because this is a new request */
  return 0;
}

/**
 * @brief called by parser when http headers ends
 *
 * @param parser
 * @return int
 */
int on_headers_complete(http_parser *parser) {
  session *ses = (session *)parser->data;
  ses->rqst.end->next = (header *)malloc(sizeof(header));
  ses->rqst.end = ses->rqst.end->next;
  ses->rqst.end->field = NULL;
  ses->rqst.end->value = NULL;
  ses->rqst.end->next = NULL;
  ses->rqst.headers_size++;
  return 0;
}

/**
 * @brief called by parser when http message ends
 *
 * @param parser
 * @return int
 */
int on_message_complete(http_parser *parser) {
  session *ses = (session *)parser->data;
  uv_read_stop((uv_stream_t *)&ses->connection);

  char *resp =
      "HTTP/1.1 200 OK\r\n"
      "Server: SimpleHTTPD\r\n"
      "Content-length: 2\r\n"
      "\r\n"
      "OK\r\n"
      "\r\n"; /** < a simple response. replace it by your request handler*/

  SSL_write(ses->ssl, resp,
            strlen(resp)); /** < write response to SSL engine, it encrypts it
                              and puts result into outgoing BIO */
  flush_write_bio(ses->wbio, &ses->connection);
  return 0;
}

/**
 * @brief called by parser when http encounters the requested URL
 *
 * @param parser
 * @param at
 * @param length
 * @return int
 */
int on_url(http_parser *parser, const char *at, size_t length) {
  session *ses = (session *)parser->data;
  request *rqst = &ses->rqst;
  if (rqst->url ==
      NULL) { /** < if it is the begining of url, allocate a memory for it */
    rqst->url =
        (char *)malloc(length + 1); /** < one byte more for NULL terminator */
    memset(rqst->url, 0, length + 1);
    memcpy(rqst->url, at, length);
  } else { /** < else realloc it by length */
    realloc(rqst->url, length);
    char *location = rqst->url + strlen(rqst->url);
    memcpy(location + 1, at, length);
    *(rqst->url + 1 + length) = 0;
  }
  return 0;
}

/**
 * @brief called by parser when http encounters a header field name
 *
 * @param parser
 * @param at
 * @param length
 * @return int
 */
int on_header_field(http_parser *parser, const char *at, size_t length) {
  session *ses = (session *)parser->data;
  request *rqst = &ses->rqst;
  if (rqst->headers == NULL) {
    rqst->headers = (header *)malloc(sizeof(header));
    rqst->headers->field = (char *)malloc(length + 1);
    memset(rqst->headers->field, 0, length + 1);
    memcpy(rqst->headers->field, at, length);
    rqst->headers->next = NULL;
    rqst->end = rqst->headers;
    return 0;
  }
  if (ses->parsing_status == VALUE) {
    rqst->headers_size++;
    rqst->end->next = (header *)malloc(sizeof(header));
    rqst->end = rqst->end->next;
    ses->parsing_status = FIELD;
    rqst->end->field = (char *)malloc(length + 1);
    memset(rqst->end->field, 0, length + 1);
    memcpy(rqst->end->field, at, length);
  } else {
    realloc(rqst->end->field, strlen(rqst->end->field) + length);
    memset(rqst->end->field + strlen(rqst->end->field) + 1, 0, length);
    memcpy(rqst->end->field + strlen(rqst->end->field) + 1, at, length);
  }
  return 0;
}

/**
 * @brief called by parser when http encounters a header field value
 *
 * @param parser
 * @param at
 * @param length
 * @return int
 */
int on_header_value(http_parser *parser, const char *at, size_t length) {
  session *ses = (session *)parser->data;
  request *rqst = &ses->rqst;

  if (ses->parsing_status == FIELD) {
    ses->parsing_status = VALUE;
    rqst->end->value = (char *)malloc(length + 1);
    memset(rqst->end->value, 0, length + 1);
    memcpy(rqst->end->value, at, length);
  } else {
    realloc(rqst->end->value, strlen(rqst->end->value) + length);
    memset(rqst->end->value + strlen(rqst->end->value) + 1, 0, length);
    memcpy(rqst->end->value + strlen(rqst->end->value) + 1, at, length);
  }
  return 0;
}

/**
 * @brief called by parser when http body begins
 *
 * @param parser
 * @param at
 * @param length
 * @return int
 */
int on_body(http_parser *parser, const char *at, size_t length) {
  session *ses = (session *)parser->data;
  request *rqst = &ses->rqst;

  if (rqst->body == NULL) {
    rqst->body = (char *)malloc(length);
    memcpy(rqst->body, at, length);
    rqst->body_lenght = length;
  } else {
    realloc(rqst->body, rqst->body_lenght + length);
    memcpy(rqst->body + rqst->body_lenght + 1, at, length);
    rqst->body_lenght += length;
  }
  return 0;
}

/*********************************
 * UV functions and callbacks
 * ******************************/
void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = (char *)malloc(suggested_size);
  buf->len = suggested_size;
}

/**
 * @brief read callback
 *
 * @param client
 * @param nread
 * @param buf
 */
void read_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  session *ses = (session *)client;
  if (nread > 0) { /** if no error ... */
    char *ebuff = (char *)malloc(256);
    // printf("Num Read: %d\n", nread);
    BIO_write(ses->rbio, buf->base, nread);
    if (!SSL_is_init_finished(
            ses->ssl)) { /** if ssl handshake is not complete */
      printf("\n+ HandShake is not complete\n");

      int error = SSL_get_error(ses->ssl, SSL_do_handshake(ses->ssl));
      printf("+ State: %s\n", SSL_state_string_long(ses->ssl));
      if (error == SSL_ERROR_WANT_WRITE || error == SSL_ERROR_WANT_READ) {
        printf("+ WANT IO\n");
      }
      if (error == SSL_ERROR_NONE) {
        int num = SSL_pending(ses->ssl);
        printf("+ NO ERROR: %d Bytes ready to decrypt\n", num);
      }
      printf("+ PENDING BYTES TO WRITE: %d\n", BIO_pending(ses->wbio));

      flush_write_bio(ses->wbio, &ses->connection);

    } else {
      printf("+ Handshake is finished, read more\n");
      int num = SSL_pending(ses->ssl);
      printf("+ %d Bytes ready to decrypt\n", num);

      char *ff = (char *)malloc(1024);
      num = SSL_read(ses->ssl, ff, 1024);
      http_parser_execute(&ses->parser, &ses->parser_settings, ff, num);
      free(ff);
    }
  }
}

void on_new_connection(uv_stream_t *server, int status) {
  if (status == -1) {
    // error!
    return;
  }

  printf("+ New connection\n");
  session *new_session = (session *)malloc(sizeof(session));
  memset((void *)new_session, 0, sizeof(session));
  uv_tcp_init(loop, &new_session->connection);
  if (uv_accept(server, (uv_stream_t *)&new_session->connection) == 0) {
    http_parser_init(&new_session->parser, HTTP_REQUEST);
    http_parser_settings_init(&new_session->parser_settings);

    new_session->parser_settings.on_body = on_body;
    new_session->parser_settings.on_header_field = on_header_field;
    new_session->parser_settings.on_header_value = on_header_value;
    new_session->parser_settings.on_headers_complete = on_headers_complete;
    new_session->parser_settings.on_message_begin = on_message_begin;
    new_session->parser_settings.on_message_complete = on_message_complete;
    new_session->parser_settings.on_url = on_url;

    new_session->parser.data = (void *)new_session;

    new_session->ssl = SSL_new(sslContext);
    SSL_set_accept_state(new_session->ssl);
    new_session->rbio = BIO_new(BIO_s_mem());
    new_session->wbio = BIO_new(BIO_s_mem());
    BIO_set_nbio(new_session->rbio, 1);
    BIO_set_nbio(new_session->wbio, 1);

    SSL_set_bio(new_session->ssl, new_session->rbio, new_session->wbio);

    uv_read_start((uv_stream_t *)new_session, alloc_buffer, read_cb);
  } else {
    uv_close((uv_handle_t *)&new_session->connection, NULL);
  }
}

/************************************
 * main function
 * *********************************/
int main(int argc, char **argv) {
  loop = uv_default_loop();
  uv_tcp_t server;
  uv_tcp_init(loop, &server);

  struct sockaddr_in bind_addr;
  init_ssl(CERT, KEY);

  uv_ip4_addr("0.0.0.0", 800, &bind_addr);
  uv_tcp_bind(&server, (const struct sockaddr *)&bind_addr, 0);
  int r;
  if ((r = uv_listen((uv_stream_t *)&server, 128, on_new_connection))) {
    fprintf(stderr, "Listen error %s\n", uv_err_name(r));
    return 2;
  }
  return uv_run(loop, UV_RUN_DEFAULT);
}
