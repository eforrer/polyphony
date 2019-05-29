#include "ev.h"
#include <sys/socket.h>

static VALUE BasicSocket_send(int argc, VALUE *argv, VALUE io);
static VALUE BasicSocket_recv(int argc, VALUE *argv, VALUE io);

void Init_Socket() {
  rb_require("socket");
  VALUE cBasicSocket = rb_const_get(rb_cObject, rb_intern("BasicSocket"));

  rb_define_method(cBasicSocket, "send", BasicSocket_send, -1);
  rb_define_method(cBasicSocket, "recv", BasicSocket_recv, -1);
}

///////////////////////////////////////////////////////////////////////////

struct rsock_send_arg {
    int fd, flags;
    VALUE mesg;
    struct sockaddr *to;
    socklen_t tolen;
};

#define StringValue(v) rb_string_value(&(v))
#define IS_ADDRINFO(obj) rb_typeddata_is_kind_of((obj), &addrinfo_type)

VALUE
rsock_sockaddr_string_value(volatile VALUE *v)
{
    // VALUE val = *v;
    // if (IS_ADDRINFO(val)) {
    //     *v = addrinfo_to_sockaddr(val);
    // }
    StringValue(*v);
    return *v;
}

#define SockAddrStringValue(v) rsock_sockaddr_string_value(&(v))
#define RSTRING_LENINT(str) rb_long2int(RSTRING_LEN(str))
#ifndef RSTRING_SOCKLEN
#  define RSTRING_SOCKLEN (socklen_t)RSTRING_LENINT
#endif

#if defined __APPLE__
# define do_write_retry(code) do {ret = code;} while (ret == -1 && errno == EPROTOTYPE)
#else
# define do_write_retry(code) ret = code
#endif

VALUE
rsock_sendto_blocking(void *data)
{
    struct rsock_send_arg *arg = data;
    VALUE mesg = arg->mesg;
    ssize_t ret;
    do_write_retry(sendto(arg->fd, RSTRING_PTR(mesg), RSTRING_LEN(mesg),
			  arg->flags, arg->to, arg->tolen));
    return (VALUE)ret;
}

VALUE
rsock_send_blocking(void *data)
{
    struct rsock_send_arg *arg = data;
    VALUE mesg = arg->mesg;
    ssize_t ret;
    do_write_retry(send(arg->fd, RSTRING_PTR(mesg), RSTRING_LEN(mesg),
			arg->flags));
    return (VALUE)ret;
}

///////////////////////////////////////////////////////////////////////////

static VALUE BasicSocket_send(int argc, VALUE *argv, VALUE sock) {
  VALUE underlying_socket = rb_iv_get(sock, "@socket");
  if (!NIL_P(underlying_socket)) sock = underlying_socket;
  struct rsock_send_arg arg;
  VALUE flags, to;
  rb_io_t *fptr;
  ssize_t n;
  rb_blocking_function_t *func;
  const char *funcname;
  VALUE write_watcher = Qnil;
  
  rb_scan_args(argc, argv, "21", &arg.mesg, &flags, &to);

  StringValue(arg.mesg);
  if (!NIL_P(to)) {
    SockAddrStringValue(to);
    to = rb_str_new4(to);
    arg.to = (struct sockaddr *)RSTRING_PTR(to);
    arg.tolen = RSTRING_SOCKLEN(to);
    func = rsock_sendto_blocking;
    funcname = "sendto(2)";
  }
  else {
    func = rsock_send_blocking;
    funcname = "send(2)";
  }
  GetOpenFile(sock, fptr);
  rb_io_set_nonblock(fptr);
  arg.fd = fptr->fd;
  arg.flags = NUM2INT(flags);
  while ((n = (ssize_t)func(&arg)) < 0) {
    if (write_watcher == Qnil)
      write_watcher = IO_write_watcher(sock);
    EV_IO_await(write_watcher);
  }
  return SSIZET2NUM(n);
}

static VALUE BasicSocket_recv(int argc, VALUE *argv, VALUE sock) {
  VALUE underlying_socket = rb_iv_get(sock, "@socket");
  if (!NIL_P(underlying_socket)) sock = underlying_socket;
  long len = argc >= 1 ? NUM2LONG(argv[0]) : 8192;
  if (len < 0) {
    rb_raise(rb_eArgError, "negative length %ld given", len);
  }

  rb_io_t *fptr;
  long n;
  int shrinkable;
  VALUE read_watcher = Qnil;


  VALUE str = argc >= 3 ? argv[2] : Qnil;

  shrinkable = io_setstrbuf(&str, len);
  OBJ_TAINT(str);
  GetOpenFile(sock, fptr);
  // rb_io_set_nonblock(fptr);
  rb_io_check_byte_readable(fptr);

  if (len == 0)
  	return str;

  while (1) {
    n = recv(fptr->fd, RSTRING_PTR(str), len, MSG_DONTWAIT);
    if (n < 0) {
      int e = errno;
      if (e == EWOULDBLOCK || e == EAGAIN) {
        if (read_watcher == Qnil)
          read_watcher = IO_read_watcher(sock);
        EV_IO_await(read_watcher);
      }
      else
        rb_syserr_fail(e, strerror(e));
        // rb_syserr_fail_path(e, fptr->pathv);
    }
    else
      break;
  }

  io_set_read_length(str, n, shrinkable);
  io_enc_str(str, fptr);

  if (n == 0)
    return Qnil;

  return str;
}