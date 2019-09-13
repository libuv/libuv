#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <uv.h>

#include <http_parser.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

uv_loop_t *loop;
SSL_CTX *sslContext;
const SSL_METHOD *sslMethod;

void init_ssl() {
  SSL_library_init();
  OpenSSL_add_all_algorithms();
  SSL_load_error_strings();
  ERR_load_BIO_strings();
  ERR_load_crypto_strings();

  sslMethod = TLSv1_2_method();
  sslContext = SSL_CTX_new(sslMethod);
  SSL_CTX_set_verify(sslContext, SSL_VERIFY_NONE, NULL);
  SSL_CTX_use_certificate_file(sslContext, "cert.pem", SSL_FILETYPE_PEM) == 1
      ? printf("SSL CERT OK\n")
      : printf("SSL CERT ERROR\n");
  SSL_CTX_use_PrivateKey_file(sslContext, "key.pem", SSL_FILETYPE_PEM) == 1
      ? printf("SSL KEY OK\n")
      : printf("SSL KEY ERROR\n");
  ;
}

typedef struct header {
  char *field;
  char *value;
  struct header *next;
} header;

typedef struct {
  char *method;
  char *url;
  header *headers;
  size_t headers_size;
  header *end;
  char *body;
  size_t body_lenght;
} request;

enum parsing_stat { FIELD = 0, VALUE };

typedef struct {
  uv_tcp_t connection;
  request rqst;
  http_parser parser;
  http_parser_settings parser_settings;
  enum parsing_stat parsing_status;

  SSL *ssl;
  BIO *rbio;
  BIO *wbio;

} session;

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

/*********************************
 * HTTP_Parser functions and callbacks
 * ******************************/
void free_write_req(uv_write_t *req) {
  write_req_t *wr = (write_req_t *)req;
  free(wr->buf.base);
  free(wr);
}

void write_cb(uv_write_t *req, int status) { free_write_req(req); }

int on_message_begin(http_parser *parser) {
  session *ses = (session *)parser->data;
  memset(&ses->rqst, 0, sizeof(request));
  return 0;
}

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

int on_message_complete(http_parser *parser) {
  session *ses = (session *)parser->data;
  uv_read_stop((uv_stream_t *)&ses->connection);

  char *resp = "HTTP/1.1 200 "
               "OK\r\nserver:SimpleHTTPD\r\nContent-length:2\r\n\r\nOK\r\n\r\n";
  uv_buf_t buf;
  buf.base = resp;
  buf.len = strlen(resp);
  uv_write_t req1;
  uv_write(&req1, (uv_stream_t *)&ses->connection, &buf, 1, write_cb);
  header *hdr = ses->rqst.headers;
  return 0;
}
int on_chunk_header(http_parser *parser);
int on_chunk_complete(http_parser *parser);

int on_url(http_parser *parser, const char *at, size_t length) {
  session *ses = (session *)parser->data;
  request *rqst = &ses->rqst;
  if (rqst->url == NULL) {
    rqst->url = (char *)malloc(length + 1);
    memset(rqst->url, 0, length + 1);
    memcpy(rqst->url, at, length);
  } else {
    realloc(rqst->url, length);
    char *location = rqst->url + strlen(rqst->url);
    memcpy(location + 1, at, length);
    *(rqst->url + 1 + length) = 0;
  }
  return 0;
}
int on_status(http_parser *parser, const char *at, size_t length);
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

void read_cb(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  session *ses = (session *)client;
  if (nread > 0) {
    char *ebuff = (char *)malloc(256);
    printf("Num Read: %d\n", nread);
    BIO_write(ses->rbio, buf->base, nread);
    if (!SSL_is_init_finished(ses->ssl)) {
      printf("HandShake is not complete\n");

      int error = SSL_get_error(ses->ssl, SSL_do_handshake(ses->ssl));
      printf("State: %s\n", SSL_state_string_long(ses->ssl));
      if (error == SSL_ERROR_WANT_WRITE) {
        printf("WANT WRITE -->\n");
      }
      if (error == SSL_ERROR_WANT_READ) {
        printf("WANT READ <--\n");
      }
      if (error == SSL_ERROR_NONE) {
        int num = SSL_pending(ses->ssl);
        printf("NO ERROR: %d Bytes ready to decrypt\n", num);
      }
      printf("PENDING: %d\n", BIO_pending(ses->wbio));

      write_req_t *req = (write_req_t *)malloc(sizeof(write_req_t));

      req->buf.base = (char *)malloc(BIO_pending(ses->wbio));
      req->buf.len = BIO_pending(ses->wbio);
      BIO_read(ses->wbio, req->buf.base, BIO_pending(ses->wbio));

      uv_write((uv_write_t *)req, (uv_stream_t *)&ses->connection, &req->buf, 1,
               write_cb);

    } else {
      BIO_write(ses->rbio, buf->base, nread);
      int num = SSL_pending(ses->ssl);
      printf("%d Bytes ready to decrypt\n", num);
      http_parser_execute(&ses->parser, &ses->parser_settings, buf->base,
                          buf->len);
    }
  }
}

void on_new_connection(uv_stream_t *server, int status) {
  if (status == -1) {
    // error!
    return;
  }

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
  init_ssl();

  uv_ip4_addr("0.0.0.0", 800, &bind_addr);
  uv_tcp_bind(&server, (const struct sockaddr *)&bind_addr, 0);
  int r;
  if ((r = uv_listen((uv_stream_t *)&server, 128, on_new_connection))) {
    fprintf(stderr, "Listen error %s\n", uv_err_name(r));
    return 2;
  }
  return uv_run(loop, UV_RUN_DEFAULT);
}
