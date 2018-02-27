//
// Created by 陈国威 on 2018/2/26.
//

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
//#include "../../core/ngx_core.h"
//#include "../../http/ngx_http.h"
//#include "../../core/ngx_config.h"
//#include "../../core/ngx_conf_file.h"
//#include "../../core/ngx_string.h"
//#include "../../http/ngx_http_core_module.h"
//#include "../../http/ngx_http_request.h"
//#include "../../core/ngx_buf.h"

//static char* ngx_http_mytest(ngx_conf_t* cf, ngx_command_t* cmd, void* conf);
//static ngx_int_t ngx_http_mytest_handler(ngx_http_request_t* r);
static ngx_int_t ngx_http_myfilter_init(ngx_conf_t  *cf);
static ngx_int_t ngx_http_myfilter_header_filter(ngx_http_request_t *r);
static ngx_int_t ngx_http_myfilter_body_filter(ngx_http_request_t *r, ngx_chain_t *in);

//配置项
typedef struct {
  ngx_flag_t enable;  //是否打开该过滤模块
} ngx_http_myfilter_conf_t;

static void* ngx_http_myfilter_create_conf(ngx_conf_t* cf)
{
  ngx_http_myfilter_conf_t	*mycf;

  // 创建存储配置项的结构体
  mycf = (ngx_http_myfilter_conf_t*)ngx_pcalloc (cf->pool, sizeof (ngx_http_myfilter_conf_t));
  if (mycf == NULL)
    {
      return NULL;
    }

  // ngx_flat_t 类型的变量。如果使用预设函数 ngx_conf_set_flag_slot 解析配置项参数，那么必须
  // 初始化为 NGX_CONF_UNSET
  mycf->enable = NGX_CONF_UNSET;


  return mycf;
}

static char* ngx_http_myfilter_merge_conf(ngx_conf_t *cf, void *parent, void *child)
{
  ngx_http_myfilter_conf_t  *prev = (ngx_http_myfilter_conf_t *)parent;
  ngx_http_myfilter_conf_t  *conf = (ngx_http_myfilter_conf_t *)child;

  //合并 ngx_flag_t 类型的配置项 enable
  ngx_conf_merge_value (conf->enable, prev->enable, 0);

  return NGX_CONF_OK;
}

//上下文
typedef struct {
  // 当add_prefix 为0时，表示不需要在放回的包体前加前缀
  // 当add_prefix 为1时，表示应当在包体前加前缀
  // 当add_prefix 为2时，表示已经添加过前缀
  // 包体处理方法在 1个请求可能被多次调用，头部处理方法在1个请求中只会被调用1次
  ngx_int_t   add_prefix;
} ngx_http_myfilter_ctx_t;

static ngx_command_t ngx_http_myfilter_commands[] = {
    {
        ngx_string ("add_prefix"),
        NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LMT_CONF|NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof (ngx_http_myfilter_conf_t, enable),
        NULL
    },

    ngx_null_command
};

static ngx_http_module_t ngx_http_myfilter_module_ctx = {
    NULL,                     /* preconfiguration 方法 */
    ngx_http_myfilter_init,   /* postconfiguration 方法 */

    NULL,                     /* create_main_conf 方法 */
    NULL,                     /* init_main_conf 方法 */

    NULL,                     /* create_srv_conf 方法 */
    NULL,                     /* merge_srv_conf 方法 */

    ngx_http_myfilter_create_conf,                     /* create_loc_conf 方法 */
	ngx_http_myfilter_merge_conf                        /* merge_loc_conf 方法 */
};

ngx_module_t  ngx_http_myfilter_module = {
    NGX_MODULE_V1,
    &ngx_http_myfilter_module_ctx,
    ngx_http_myfilter_commands,
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

static ngx_http_output_header_filter_pt ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt   ngx_http_next_body_filter;


static ngx_int_t ngx_http_myfilter_init(ngx_conf_t  *cf)
{
  // 插入到头部处理方法链表的首部
  ngx_http_next_header_filter = ngx_http_top_header_filter;
  ngx_http_top_header_filter = ngx_http_myfilter_header_filter;

  //插入到包体处理方法链表的首部
  ngx_http_next_body_filter = ngx_http_top_body_filter;
  ngx_http_top_body_filter = ngx_http_myfilter_body_filter;

  return NGX_OK;
}

//在 http 响应体的包体前加的前缀字符串硬编码为 filter_prefix 变量
static ngx_str_t filter_prefix = ngx_string ("[my filter prefix]");

static ngx_int_t ngx_http_myfilter_header_filter(ngx_http_request_t *r)
{
  ngx_http_myfilter_ctx_t   *ctx;
  ngx_http_myfilter_conf_t  *conf;

  /* 如果不是返回成功，那么这时时不需要理会是否加前缀的，直接交由下
   * 一个过滤模块处理响应码非200 的情况
   * */
  if (r->headers_out.status != NGX_HTTP_OK)
    {
      return ngx_http_next_header_filter(r);
    }

  // 获取 http 上下文
  ctx = ngx_http_get_module_ctx (r, ngx_http_myfilter_module);
  if (ctx)
    {
      /* 该请求的上下文已经存在，这说明 ngx_http_myfilter_header_filter
       * 已经被调用过1次， 直接交由下一个模块过滤处理
       * */
      return ngx_http_next_header_filter(r);
    }

  // 获取存储配置项的 ngx_http_myfilter_conf_t 结构体
  conf = ngx_http_get_module_loc_conf (r, ngx_http_myfilter_module);
  /* 如果enable 成员为0，也就是配置文件中没有配置 add_prefix 配置项，或者 add_prefix 配置项的
   * 参数值是off, 那么直接交由下一个过滤模块处理
   * */
  if (conf->enable == 0)
    {
      return ngx_http_next_header_filter(r);
    }

  // 构造 http 上下文结构体 ngx_http_myfilter_ctx_t
  ctx = ngx_pcalloc (r->pool, sizeof (ngx_http_myfilter_ctx_t));
  if (ctx == NULL)
    {
      return NGX_ERROR;
    }

  //add_prefix 为0 表示不加前缀
  ctx->add_prefix = 0;
  ngx_http_set_ctx (r, ctx, ngx_http_myfilter_module);

  // myfilter 过滤模块只处理 Content-Type 是"text/plain" 类型的 http响应
  if (r->headers_out.content_type.len >= sizeof ("text/plain") -1 &&
      ngx_strncasecmp (r->headers_out.content_type.data, (u_char*)"text/plain", sizeof ("text/plain") -1) == 0)
  {
    // 设置为1 表示需要在http 包体前加入前缀
    ctx->add_prefix = 1;

    /* 当处理模块已经在 Content-Length 中写入 http 包体的长度时，
     * 由于我们加入了前缀字符串，需要把这个字符串的长度加入到 Content-Length 中
     * */
    if (r->headers_out.content_length_n > 0)
      {
        r->headers_out.content_length_n += filter_prefix.len;
      }

  }

    return ngx_http_next_header_filter(r);
}

//包体处理方法
static ngx_int_t ngx_http_myfilter_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
  ngx_http_myfilter_ctx_t   *ctx;
  ctx = ngx_http_get_module_ctx (r, ngx_http_myfilter_module);

  /* 如果获取不到上下文，或者上下文结构体中的 add_prefix 为0或者为 2 时，都不会添加前缀，
   * 这时直接交给下一个 http 过滤模块处理
   * */
  if (ctx == NULL || ctx->add_prefix != 1)
    {
      return ngx_http_next_body_filter(r, in);
    }

  /* 将add_prefix 设置为2， 这样即使ngx_http_myfilter_body_filter 再次回调时，也不会重复添加前缀*/
  ctx->add_prefix = 2;

  //从请求的内存池中分配内存，用于存储字符串前缀
  ngx_buf_t *b = ngx_create_temp_buf (r->pool, filter_prefix.len);
  // 将ngx_buf_t 中的指针正确地指向 filter_prefix 字符串
  b->start = b->pos = filter_prefix.data;
  b->last = b->pos + filter_prefix.len;

  /* 从请求的内存池中生成 ngx_chain_t 链表，将分配的 ngx_buf_t 设置到 buf成员中
   * 并将它添加到原先待发送的 http 包体前面
   * */
  ngx_chain_t *cl = ngx_alloc_chain_link (r->pool);
  cl->buf = b;
  cl->next = in;

  // 调用下一个模块的 http 包体处理方法， 注意，这时传入的时新生成的 cl链表
  return ngx_http_next_body_filter(r, cl);
}








