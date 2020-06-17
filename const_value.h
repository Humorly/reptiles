#pragma once

// 返回值标记
enum
{
	invalid_ret_val = 1,
	valid_ret_val = 0,
	// http请求正确代码
	valid_http_ret_val = 200,
	// 最大下载文件大小
	max_content_size = 1024 * 1024 * 4,
};
 
