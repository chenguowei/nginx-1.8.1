//
// Created by 陈国威 on 2018/2/11.
//


#include <stddef.h>
#include <sys/_types/_off_t.h>
#include "../../core/ngx_string.h"
#include "../../core/ngx_config.h"
#include "../../core/ngx_array.h"
#include "../../os/unix/ngx_time.h"
#include "../../core/ngx_buf.h"
#include "../../core/ngx_file.h"
#include "../../core/ngx_core.h"
#include "../../http/ngx_http_config.h"
#include "../../core/ngx_conf_file.h"
#include "../../core/ngx_palloc.h"
#include "../../core/ngx_log.h"
#include "../../http/ngx_http.h"
#include "../../http/ngx_http_request.h"

#include <time.h>
#include <ntsid.h>

typedef struct {
  ngx_str_t		my_str;
  ngx_int_t 	my_num;
  ngx_flag_t	my_flag;
  size_t 		my_size;
  ngx_array_t*	my_str_array;
  ngx_array_t*	my_keyval;
  off_t 		my_off;
  ngx_msec_t 	my_msec;
  time_t		my_sec;
  ngx_bufs_t	my_bufs;
  ngx_uint_t 	my_enum_seq;
  ngx_uint_t 	my_bitmask;
  ngx_uint_t 	my_access;
  ngx_path_t*	my_path;
  ngx_str_t		my_config_str;
  ngx_int_t 	my_config_num;

} ngx_http_mytest_conf_t;


static void* ngx_http_mytest_create_loc_conf(ngx_conf_t* cf)
{
  ngx_http_mytest_conf_t	*myconf;

  myconf = (ngx_http_mytest_conf_t*)ngx_pcalloc (cf->pool, sizeof (ngx_http_mytest_conf_t));
  if (myconf == NULL)
	{
	  return NULL;
	}

  myconf->my_flag = NGX_CONF_UNSET;
  myconf->my_num = NGX_CONF_UNSET;
  myconf->my_str_array = NGX_CONF_UNSET_PTR;
  myconf->my_keyval = NULL;
  myconf->my_off = NGX_CONF_UNSET;
  myconf->my_msec = NGX_CONF_UNSET_MSEC;
  myconf->my_sec = NGX_CONF_UNSET;
  myconf->my_size = NGX_CONF_UNSET_SIZE;

  return myconf;
}

static ngx_conf_enum_t	test_enums[] = {
	{ ngx_string("apple"), 1},
	{ ngx_string("banana"), 2},
	{ ngx_string("orange"), 3},
	{ ngx_null_string, 0}
};

static ngx_conf_bitmask_t test_bitmasks[] = {
	{ ngx_string ("good"), 0x0002},
	{ ngx_string ("better"), 0x0004},
	{ ngx_string ("best"), 0x0008},
	{ ngx_null_string, 0}
};

static ngx_command_t	ngx_http_mytest_commands[] = {
	{//只出现在 location{} 块中, 携带1个参数，值只能是on, off
		ngx_string ("test_flag"),
		NGX_HTTP_LOC_CONF |  NGX_CONF_FLAG,
		ngx_conf_set_flag_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_flag),
		NULL
	},
	{ //可以出现在 http{} server{} location{} 块中，携带1个参数
		ngx_string ("test_str"),
		NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_str_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_str),
		NULL
	},
	{	//只出现在 location{}, 携带1个参数 有多个同名配置项
		ngx_string ("test_str_array"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_str_array_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_str_array),
		NULL
	},
	{	//只出现在 locatation{} 中，多个同名的 key-value 配置
		ngx_string ("test_keyval"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
		ngx_conf_set_keyval_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof(ngx_http_mytest_conf_t, my_keyval),
		NULL
	},
	{	// 必须携带1个参数， 这个参数值必须为 整数
		ngx_string ("test_num"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_num_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_num),
		NULL
	},
	{	//只允许配置项后的参数携带单位 k,K,m ,M
		ngx_string ("test_size"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_size_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_size),
		NULL
	},
	{
		ngx_string ("test_off"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_off_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_off),
		NULL
	},
	{
		ngx_string ("test_msec"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_msec_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_msec),
		NULL
	},
	{
		ngx_string ("test_sec"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_sec_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_sec),
		NULL
	},
	{
		ngx_string ("test_bufs"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE2,
		ngx_conf_set_bufs_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_bufs),
		NULL
	},
	{
		ngx_string ("test_enum"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_enum_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_enum_seq),
		test_enums,
	},
	{
		ngx_string ("test_bitmask"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
		ngx_conf_set_bitmask_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_bitmask),
		test_bitmasks
	},
	{
		ngx_string ("test_access"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE123,
		ngx_conf_set_access_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_access),
		NULL
	},
	{
		ngx_string ("test_path"),
		NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1234,
		ngx_conf_set_path_slot,
		NGX_HTTP_LOC_CONF_OFFSET,
		offsetof (ngx_http_mytest_conf_t, my_path),
		NULL
	},

	ngx_null_command

};

static char* ngx_conf_set_myconfig(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
  /*参数conf 就是 HTTP 框架传给用户的 ngx_http_mytest_create_loc_conf
   *  回调方法中分配的结构体 ngx_http_mytest_conf_t
   * */
  ngx_http_mytest_conf_t	*mycf = conf;

  /*
   * cf->args 是1个ngx_array_t 队列， 它的成员都是ngx_str_t结构体
   * */
  ngx_str_t*	value = cf->args->elts;

  //ngx_array_t 的 netls 表示参数的个数
  if (cf->args->nelts > 1)
	{
	  mycf->my_config_str = value[1];
	}
  if (cf->args->nelts > 2)
	{
	  mycf->my_config_num = ngx_atoi (value[2].data, value[2].len);
	  if (mycf->my_config_num == NGX_ERROR)
		{
		  return "invalid number";
		}
	}

  //测试日志
  long t1 = 490000000;
  u_long tul = 50000000;
  int32_t ti32 = 110;
  ngx_str_t	tstr = ngx_string ("teststr");
  double tdoub = 3.1415926;
  int x = 15;
  ngx_log_error(NGX_LOG_ALERT, cf->log, 0,
  			"l=%l, ul=%ul, D=%D, p=%p, f=%.10f, str=%V,x=%xd, X=%Xd",
  			t1, tul, ti32, &ti32, tdoub, &tstr, x, x);


  return NGX_CONF_OK;
}


typedef struct
{
  ngx_int_t 	my_step;
} ngx_http_mytest_ctx_t;

static ngx_int_t ngx_http_mytest_handler(ngx_http_request_t *r)
{
  // 首先调用 ngx_http_get_module_ctx 宏来获取上下文结构体
  ngx_http_mytest_ctx_t*	myctx = ngx_http_get_module_ctx (r, ngx_http_mytest_module);

  //如果之前没有设置过上下文，那么应当返回 NULL
  if (myctx == NULL)
	{
	  // 必须在当前请求的内存池中分配上下文结构体，这样请求结束时会自动释放
	  myctx = ngx_palloc (r->pool, sizeof (ngx_http_mytest_ctx_t));
	  if (myctx == NULL)
		{
		  return NGX_ERROR;
		}

	  ngx_http_set_ctx (r, myctx, ngx_http_mytest_module);
	}
}

