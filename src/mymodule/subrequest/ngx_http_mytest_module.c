//
// Created by 陈国威 on 2018/2/26.
//

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct {
  ngx_str_t		stock[6];
} ngx_http_mytest_ctx_t;


static char* ngx_http_mytest(ngx_conf_t* cf, ngx_command_t* cmd, void* conf);
static ngx_int_t ngx_http_mytest_handler(ngx_http_request_t *r);
static void mytest_post_handler(ngx_http_request_t *r);


static ngx_command_t ngx_http_mytest_commands[] = {
    {
        ngx_string ("mytest"),
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_NOARGS,
        ngx_http_mytest,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        NULL,
    },

    ngx_null_command
};

static ngx_http_module_t ngx_http_mytest_module_ctx = {
    NULL,
    NULL,

    NULL,
    NULL,

    NULL,
    NULL,

    NULL,
    NULL
};

ngx_module_t  ngx_http_mytest_module = {
    NGX_MODULE_V1,
    &ngx_http_mytest_module_ctx,
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


static char* ngx_http_mytest(ngx_conf_t* cf, ngx_command_t* cmd, void *conf)
{
  ngx_http_core_loc_conf_t  *clcf;

  // 首先找到 mytest 配置项所属的配置块
  clcf = ngx_http_conf_get_module_loc_conf (cf, ngx_http_core_module);

  clcf->handler = ngx_http_mytest_handler;

  return NGX_CONF_OK;
}

static ngx_int_t mytest_subrequest_post_handler(ngx_http_request_t *r, void* data, ngx_int_t rc)
{
  // 当前请求r 是子请求， 它的 parent 成员指向父请求
  ngx_http_request_t	*pr = r->parent;

  ngx_http_mytest_ctx_t*	myctx = ngx_http_get_module_ctx (pr, ngx_http_mytest_module);

  pr->headers_out.status = r->headers_out.status;
  /* 如果返回 NGX_HTTP_OK ， 则意味着访问新浪服务器成功，接着将开始解析http 包体 */
  if (r->headers_out.status == NGX_HTTP_OK)
    {
      int flag = 0;

      /* 在不转发响应时，buffer 中会保存上游服务器的响应，特别是在使用反向代理模块访问上游服务器时，
       * 如果它使用 upstream 机制时没有重定义 input_filter 方法，upstream 机制默认的 input_filter
       * 方法会试图把所有的上游响应全部保存到 buffer 缓冲区中
       * */
      ngx_buf_t*  pRecvBuf = &r->upstream->buffer;

      /* 以下开始解析上游服务器的响应，并将解析出的值赋到上下文结构体 myctx->stock 数组中 */
	  for (; pRecvBuf->pos != pRecvBuf->last; pRecvBuf->pos++)
        {
          if (*pRecvBuf->pos == ',' || *pRecvBuf->pos == '\"')
            {
              if (flag > 0)
                {
                  myctx->stock[flag-1].len = pRecvBuf->pos - myctx->stock[flag -1].data;
                }
              flag++;
              myctx->stock[flag-1].data = pRecvBuf->pos+1;
            }

          if (flag > 6)
            break;
        }
    }


  // 设置接下来父请求的回调方法，这一步很重要
  pr->write_event_handler = mytest_post_handler;

  return NGX_OK;

}

static void mytest_post_handler(ngx_http_request_t *r)
{
  // 如果没有返回200， 则直接将错误吗发回用户
  if (r->headers_out.status != NGX_HTTP_OK)
    {
      ngx_http_finalize_request (r, r->headers_out.status);
      return;
    }

  // 当前请求是父请求，直接取其上下文
  ngx_http_mytest_ctx_t*  myctx = ngx_http_get_module_ctx (r, ngx_http_mytest_module);

  /* 定义发送给用户的 http 包体内容， */
  ngx_str_t output_format = ngx_string ("stock[%V], Today current price: %V, volumn: %V");

  // 计算待发送包体的长度
  int bodylen = output_format.len + myctx->stock[0].len + myctx->stock[1].len + myctx->stock[4].len -6;
  r->headers_out.content_length_n = bodylen;

  // 在内存池上分配内存以保存将要发送的包体
  ngx_buf_t*  b = ngx_create_temp_buf (r->pool, bodylen);
  ngx_snprintf(b->pos,bodylen, (char*)output_format.data, &myctx->stock[0], &myctx->stock[1], &myctx->stock[4]);
  b->last = b->pos + bodylen;
  b->last_buf = 1;

  ngx_chain_t out;
  out.buf = b;
  out.next = NULL;

  // 设置 Content-Type ,
  static ngx_str_t type = ngx_string ("text/plain; charset=GBK");
  r->headers_out.content_type = type;
  r->headers_out.status = NGX_HTTP_OK;

  r->connection->buffered |= NGX_HTTP_WRITE_BUFFERED;
  // 发送 http 头部给客户端
  ngx_int_t ret = ngx_http_send_header (r);
  // 发送 http 包体给客户端
  ret = ngx_http_output_filter (r, &out);

  /* 注意，这里发送完响应后必须手动调用 ngx_http_finalize_request 结束请求，因为这时http 框架不会再帮忙调用它 */
  ngx_http_finalize_request (r, ret);
}


static ngx_int_t ngx_http_mytest_handler(ngx_http_request_t *r)
{
  // 创建 http 上下文
  ngx_http_mytest_ctx_t *myctx = ngx_http_get_module_ctx (r, ngx_http_mytest_module);
  if (myctx == NULL)
    {
      myctx = ngx_palloc (r->pool, sizeof (ngx_http_mytest_ctx_t));
      if (myctx == NULL)
        {
          return NGX_ERROR;
        }

      // 将上下文设置到原始请求 r 中
      ngx_http_set_ctx (r, myctx, ngx_http_mytest_module);

    }

  // ngx_http_post_subrequest 结构体会决定子请求的回调方法
  ngx_http_post_subrequest_t *psr = ngx_palloc (r->pool, sizeof (ngx_http_post_subrequest_t));
  if (psr == NULL)
    {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

  // 设置子请求回调方法为 mytest_subrequest_post_handler
  psr->handler = mytest_subrequest_post_handler;

  /* 将 data 设为myctx 上下文，这样回调mytest_subrequest_post_handler 时传入的 data 参数就是myctx */
  psr->data = myctx;

  /* 子请求的 uri 前缀是 /list, 这时因为访问新浪服务器的请求必须是类似 /list=s_sh000001 的uri
   * ，这是与nginx.conf 中配置的子请求location 的uri 是一致的
   * */
  ngx_str_t sub_prefix = ngx_string ("/list=");
  ngx_str_t sub_location;
  sub_location.len = sub_prefix.len + r->args.len;
  sub_location.data = ngx_palloc (r->pool, sub_location.len);
  ngx_snprintf(sub_location.data, sub_location.len, "%V%V", &sub_prefix.data, &r->args);

  // sr 就是子请求
  ngx_http_request_t  *sr;

  /* 调用 ngx_http_subrequest 创建子请求， 它只会返回 NGX_OK 或者 NGX_ERROR
   * 返回 NGX_OK 时，sr 已经是合法的子请求。
   * */
  ngx_int_t rc = ngx_http_subrequest (r, &sub_location, NULL, &sr, psr, NGX_HTTP_SUBREQUEST_IN_MEMORY);
  if (rc != NGX_OK)
    {
      return NGX_ERROR;
    }

  //必须返回 NGX_DONE, 原因同 upstream
  return NGX_DONE;
}
