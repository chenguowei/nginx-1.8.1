//
// Created by 陈国威 on 2018/2/12.
//


#include <netinet/in.h>
#include <netdb.h>
#include "../../http/ngx_http.h"
#include "../../core/ngx_config.h"
#include "../../http/ngx_http_upstream.h"
#include "../../core/ngx_core.h"
#include "../../core/ngx_string.h"
#include "../../http/ngx_http_config.h"
#include "../../core/ngx_conf_file.h"
#include "../../http/ngx_http_core_module.h"
#include "../../core/ngx_palloc.h"
#include "../../core/ngx_buf.h"
#include "../../os/unix/ngx_alloc.h"
#include "../../core/ngx_hash.h"
#include "../../http/ngx_http_request.h"
#include "../../core/ngx_log.h"
#include "../../core/ngx_connection.h"
#include "../../core/ngx_list.h"

static void mytest_upstream_finalize_request(ngx_http_request_t *r, ngx_int_t rc);
ngx_int_t mytest_upstream_create_request(ngx_http_request_t *r);
ngx_int_t mytest_upstream_process_header(ngx_http_request_t *r);
static void* ngx_http_mytest_create_loc_conf(ngx_conf_t	*cf);
static char* ngx_http_mytest_merge_loc_conf(ngx_conf_t	*cf, void *parent, void *child);

static char* ngx_http_mytest(ngx_conf_t* cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_mytest_handler(ngx_http_request_t *r);

typedef struct
{
  ngx_http_upstream_conf_t	upstream;
} ngx_http_mytest_conf_t;

typedef struct
{
  ngx_http_status_t		status;
} ngx_http_mytest_ctx_t;

static ngx_command_t ngx_http_mytest_commands[] = {
	{
		ngx_string ("mytest"),
		NGX_HTTP_MAIN_CONF| NGX_HTTP_SRV_CONF| NGX_HTTP_LOC_CONF| NGX_HTTP_LMT_CONF| NGX_HTTP_NOARGS,
		ngx_http_mytest,
		NGX_HTTP_LOC_CONF_OFFSET,
		0,
		NULL
	},

	ngx_null_command
};

static ngx_http_module_t ngx_http_mytest_module_ctl = {
  NULL,
  NULL,

  NULL,
  NULL,

  NULL,
  NULL,

  ngx_http_mytest_create_loc_conf,
  ngx_http_mytest_merge_loc_conf,
};

ngx_module_t	ngx_http_mytest_module = {
	NGX_MODULE_V1,
	&ngx_http_mytest_module_ctl,
	ngx_http_mytest_commands,
	NGX_HTTP_MODULE,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NGX_MODULE_V1_PADDING
};

static void* ngx_http_mytest_create_loc_conf(ngx_conf_t	*cf)
{
  ngx_http_mytest_conf_t	*mycf;

  mycf = (ngx_http_mytest_conf_t*)ngx_pcalloc (cf->pool, sizeof (ngx_http_mytest_conf_t));
  if (mycf == NULL)
	{
	  return NULL;
	}

  /* 硬编码ngx_http_upstream_conf_t 结构中的各成员*/
  mycf->upstream.connect_timeout = 60000;
  mycf->upstream.send_timeout = 60000;
  mycf->upstream.read_timeout = 60000;
  mycf->upstream.store_access = 0600;

  /* buffering 已经决定将以固定大小的内存作为缓冲区来转发上游的响应包体，
   * 这块固定缓冲区大小就是buffer_size. 如果 buffering 为1，就会使用更多的内存缓存来不及发往下游的响应，
   *
   * */
  mycf->upstream.buffering = 0;
  mycf->upstream.bufs.num = 8;
  mycf->upstream.bufs.size = ngx_pagesize;
  mycf->upstream.buffer_size = ngx_pagesize;
  mycf->upstream.busy_buffers_size = 2 * ngx_pagesize;
  mycf->upstream.temp_file_write_size = 2 * ngx_pagesize;
  mycf->upstream.max_temp_file_size = 1024* 1024 *1024;

  /*upstream 模块要求 hide_headers 成员必须要初始化（upstream 在解析完上游服务器返回的包头时，
   * 会调用ngx_http_upstream_process_headers 方法按照hide_headers成员将本应转发给下游的 HTTP头部隐藏）
   * 这里将它赋为 NGX_CONF_UNSET_PTR, 这是为了在merge 合并配置项方法中使用 upstream 模块提供的
   * ngx_http_upstream_hide_headers_hash 方法初始化 hide_headers 成员
   * */
  mycf->upstream.hide_headers = NGX_CONF_UNSET_PTR;
  mycf->upstream.pass_headers = NGX_CONF_UNSET_PTR;

  return mycf;
}


static char* ngx_http_mytest_merge_loc_conf(ngx_conf_t	*cf, void *parent, void *child)
{
  ngx_http_mytest_conf_t	*prev = (ngx_http_mytest_conf_t*)parent;
  ngx_http_mytest_conf_t	*conf = (ngx_http_mytest_conf_t*)child;

  ngx_hash_init_t		hash;
  hash.max_size = 100;
  hash.bucket_size = 1024;
  hash.name = "proxy_headers_hash";
  if (ngx_http_upstream_hide_headers_hash (cf, &conf->upstream,
  		&prev->upstream, ngx_http_proxy_hide_headers, &hash) != NGX_OK)
	{
	  return NGX_CONF_ERROR;
	}


  return NGX_CONF_OK;
}

static ngx_int_t mytest_upstream_create_request(ngx_http_request_t	*r)
{
  /* 发往google 上游服务器的请求很简单，就是模仿正常的搜索请求，以 /search?q=...
   * URL 来发起搜索请求。
   * */
  static ngx_str_t	backendQueryLine =
	  				ngx_string ("GET /search?q=%V HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n");

  ngx_int_t queryLineLen = backendQueryLine.len + r->args.len - 2;
  /*必须在内存池中申请内存，这有一下两点好处：在网络情况不佳的情况下，向上游服务器发送请求时，可能需要epoll
   * 多次调度send 才能发送完成，这时必须保证这段内存不会被释放； 另一个好处是：在请求结束是，这段内存会被
   * 自动释放，降低内存泄漏的可能
   * */
  ngx_buf_t*	b = ngx_create_temp_buf (r->pool, queryLineLen);
  if (b == NULL)
	{
	  return NGX_ERROR;
	}

  //last 要指向请求的末尾
  b->last = b->pos + queryLineLen;

  //相当于snpringf
  ngx_snprintf(b->pos, queryLineLen, (char*)backendQueryLine.data, &r->args);

  r->upstream->request_bufs = ngx_alloc_chain_link (r->pool);
  if (r->upstream->request_bufs == NULL)
	{
	  return NGX_ERROR;
	}

  r->upstream->request_bufs->buf = b;
  r->upstream->request_bufs->next = NULL;

  r->upstream->request_sent = NULL;
  r->upstream->header_sent = NULL;

  r->header_hash = 1;
  return NGX_OK;
}

static ngx_int_t mytest_process_status_line(ngx_http_request_t	*r)
{
  size_t		len;
  ngx_int_t		rc;
  ngx_http_upstream_t	*u;

  // 上下文中才会保存多次解析 HTTP 响应行的状态，
  ngx_http_mytest_ctx_t	*ctx = ngx_http_get_module_ctx (r, ngx_http_mytest_module);
  if (ctx == NULL)
	{
	  return NGX_ERROR;
	}

  u = r->upstream;

  /* HTTP 框架提供的ngx_http_parse_status_line 方法可以解析 HTTP响应行，它的输入
   * 就是收到的字符流和上下文中 ngx_http_status_t 结构
   * */
  rc = ngx_http_parse_status_line (r, &u->buffer, &ctx->status);
  //返回NGX_AGAIN 时， 表示还没有解析出完美的 HTTP 响应行，需要接收更多的字符流再进行解析
  if ( rc == NGX_AGAIN)
	{
	  return rc;
	}

  //返回NGX_ERROR时，表示没有接收到合法的 HTTP 响应行
  if (rc == NGX_ERROR)
	{
	  ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "upstream send to no valid HTTP/1.0 header");

	  r->http_version = NGX_HTTP_VERSION_9;
	  u->state->status = NGX_HTTP_OK:

	  return NGX_OK;
	}

  if (u->state)
	{
	  u->state->status = ctx->status.code;
	}

  u->headers_in.status_n = ctx->status.code;

  len = ctx->status.end - ctx->status.start;
  u->headers_in.status_line.len = len;

  u->headers_in.status_line.data = ngx_pnalloc (r->pool, len);
  if (u->headers_in.status_line.data == NULL)
	{
	  return NGX_ERROR;
	}

  ngx_memcpy (u->headers_in.status_line.data, ctx->status.start, len);

  //下一步将开始解析 HTTP头部。设置 process_header 回调方法为 mytest_upstream_process_header,
  u->process_header = mytest_upstream_process_header;

  return mytest_upstream_process_header (r);

}

//处理上游放回的 http 响应头部
static ngx_int_t mytest_upstream_process_header(ngx_http_request_t	*r)
{
  ngx_int_t 			rc;
  ngx_table_elt_t		*h;
  ngx_http_upstream_header_t	*hh;
  ngx_http_upstream_main_conf_t	*umcf;

  /*这里将upstream 模块配置项ngx_http_upstream_main_conf_t 取出来，目的只有一个，就是对
   * 将要转发给下游客户端的 HTTP响应头部进行统一处理。该结构体中存储来需要进行统一处理的HTTP
   * 头部名称和回调方法
   *
   * */

  //循环地解析所有的 HTTP头部
  for (; ;)
	{
	  // HTTP框架提供来基础性的ngx_http_parse_header_line方法，它用于解析HTTP头部
	  rc = ngx_http_parse_header_line (r, &r->upstream, 1);
	  // 返回 NGX_OK 时，表示解析出一行 HTTP 头部
	  if (rc == NGX_OK)
		{
		  // 向 headers_in.headers 这个 ngx_list_t 链表中添加 HTTP头部
		  h = ngx_list_push (&r->upstream->headers_in.headers);
		  if (h == NULL)
			{
			  return NGX_ERROR;
			}
		  //下面开始构造刚刚添加到 headers 链表中的HTTP头部
		  h->hash = r->header_hash;

		  h->key.len = r->header_name_end - r->header_name_start;
		  h->value.len = r->header_end - r->header_start;

		  //必须在内存池中分配存放 HTTP 头部的内存空间
		  h->key.data = ngx_pnalloc (r->pool,
		  		h->key.len + 1 + h->value+1+ h->key.len);
		  if (h->key.data == NULL)
			{
			  return NGX_ERROR;
			}


		  h->value.data = h->key.data + h->key.len + 1;
		  h->lowcase_key = h->key.data + h->key.len + 1 + h->value.len + 1;

		  ngx_memcpy (h->key.data, r->header_name_start, h->key.len);
		  h->key.data[h->key.len] = '\0';
		  ngx_memcpy (h->value.data, r->header_start, h->value.len);
		  h->value.data[h->value.len] = '\0';

		  if (h->key.len == r->lowcase_index)
			{
			  ngx_memcpy (h->lowcase_key, r->lowcase_header, h->key.len);
			}
		  else
			{
			  ngx_strlow (h->lowcase_key, h->key.data, h->key.len);
			}

		  //upstream 模块会对一些HTTP头部做特殊处理
		  hh = ngx_hash_find (&umcf->headers_in_hash, h->hash, h->lowcase_key, h->key.len);

		  if (hh && hh->handler(r, h, hh->offset) != NGX_OK)
			{
			  return NGX_ERROR;
			}

		  continue;
		}

	  // 返回 NGX_HTTP_PARSE_HEADER_DONE时, 表示响应中的所有的http 头部都解析完毕，接下来接收的都是包体
	  if (rc == NGX_HTTP_PARSE_HEADER_DONE)
		{
		  /* 如果之前解析 http 头部时没有发现 server 和date头部，
		   * 那么下面会根据 HTTP协议规范添加这两个头部
		   * */
		  if (r->upstream->headers_in.server == NULL)
			{
			  h = ngx_list_push (r->upstream->headers_in.headers);
			  if (h == NULL)
				{
				  return NGX_ERROR;
				}

			  h->hash = ngx_hash (ngx_hash (ngx_hash (ngx_hash (ngx_hash ('s', 'e'),
																'r'), 'v'), 'e'), 'r');

			  ngx_str_set (&h->key, "Server");
			  ngx_str_null (&h->value);
			  h->lowcase_key = (u_char*)"server";
			}

		  if (r->upstream->headers_in.date = NULL)
			{
			  h = ngx_list_push (&r->upstream->headers_in.headers);
			  if (h == NULL)
				{
				  return NGX_ERROR;
				}

			  h->hash = ngx_hash (ngx_hash (ngx_hash ('d', 'a'), 't'), 'e');

			  ngx_str_set (&h->key, "Date");
			  ngx_str_null (&h->value);
			  h->lowcase_key = (u_char*)"date";
			}

		  return NGX_OK;
		}

	  //如果返回 NGX_AGAIN, 则表示状态机还没有解析到完整的 HTTP 头部，此时要求 upstream 模块继续
	  // 接收新的字节流，然后交由 process_header 回调方法解析
	  if (rc == NGX_AGAIN)
		{
		  return NGX_AGAIN;
		}

	  // 其他返回值都是非法的
	  ngx_log_error (NGX_LOG_ERR, r->connection->log, 0, "upstream sent invalid header");

	  return NGX_HTTP_UPSTREAM_INVALID_HEADER;
	}
}

static void mytest_upstream_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
  ngx_log_error (NGX_LOG_DEBUG, r->connection->log, 0, "mytest_upstream_finalize_request");
}



static char* ngx_http_mytest(ngx_conf_t* cf, ngx_command_t *cmd, void *conf)
{
  ngx_http_core_loc_conf_t	*clrf;

  clrf = ngx_http_conf_get_module_loc_conf (cf, ngx_http_core_module);

  clrf->handler = ngx_http_mytest_handler;

  return NGX_CONF_OK;

}


// 介入处理客户端请求的 ngx_http_mytest_handler
static ngx_int_t ngx_http_mytest_handler(ngx_http_request_t *r)
{

  // 首先建立 http 上下文结构体 ngx_http_mytest_ctx_t
  ngx_http_mytest_ctx_t* myctx = ngx_http_get_module_ctx (r, ngx_http_mytest_module);
  if (myctx == NULL)
	{
	  myctx = ngx_palloc (r->pool, sizeof (ngx_http_mytest_ctx_t));
	  if (myctx == NULL)
		{
		  return NGX_ERROR;
		}

	  //将新建的上下文与请求关联起来
	  ngx_http_set_ctx (r, myctx, ngx_http_mytest_module);

	}

  // 对每一个使用upstream 的请求，必须调用且只能调用1次ngx_http_upstream_create 方法
  if (ngx_http_upstream_create (r) != NGX_OK)
	{
	  ngx_log_error (NGX_LOG_ERR, r->connection->log, 0, "ngx_http_upstream_create() failed");

	  return NGX_ERROR;
	}

  //得到配置结构体 ngx_http_mytest_conf_t
  ngx_http_mytest_conf_t	*mycf = (ngx_http_mytest_conf_t*) ngx_http_get_module_loc_conf (r,
  										ngx_http_mytest_module);

  ngx_http_upstream_t	*u = r->upstream;
  //这里用配置文件中的结构体来赋给 r->upstream->conf 成员
  u->conf = &mycf->upstream;
  // 决定转发包体时使用的缓冲区
  u->buffering = mycf->upstream.buffering;

  //一下代码开始初始化 resolved 结构体， 用来保存上游服务器的地址
  u->resolved = (ngx_http_upstream_resolved_t*) ngx_pcalloc (r->pool, sizeof (ngx_http_upstream_resolved_t));
  if (u->resolved == NULL)
	{
	  ngx_log_error (NGX_LOG_ERR, r->connection->log, 0,
	  	"ngx_pnalloc resolved error. %s", strerror(errno));

	  return NGX_ERROR;
	}

  //这里的上游服务器就是 www.google.com
  static struct sockaddr_in backendSockAddr;
  struct hostent	*pHost = gethostbyname ((char*)"www.google.com");
  if (pHost == NULL)
	{
	  ngx_log_error (NGX_LOG_ERR, r->connection->log, 0, "gethostbyname fail. %s", strerror(errno));

	  return NGX_ERROR;
	}

  //访问上游服务器的 80 端口
  backendSockAddr.sin_family = AF_INET;
  backendSockAddr.sin_port = htons ((in_port_t) 80);
  char* pDmsIP = inet_ntoa (*(struct in_addr*)(pHost->h_addr_list[0]));
  backendSockAddr.sin_addr.s_addr = inet_addr(pDmsIP);

  u->resolved->sockaddr = (struct sockaddr* )&backendSockAddr;
  u->resolved->socklen = sizeof (struct sockaddr_in);
  u->resolved->naddrs = 1;

  u->create_request = mytest_upstream_create_request;
  u->process_header = mytest_process_status_line;
  u->finalize_request = mytest_upstream_finalize_request;

  r->main->count++;
  //启动 upstream
  ngx_http_upstream_init (r);

  return NGX_DONE;
}











