// reptile.cpp: 定义控制台应用程序的入口点。
//

#include <memory>
#include "spider.h"

int main()
{
	//std::shared_ptr<spider> spider_http_(new http_spider());
	//spider_http_->get_content("http://www.gx8899.com/");

	std::shared_ptr<spider> spider_https_(new https_spider());
	spider_https_->get_content("https://www.qqtn.com/tx/weixintx_1.html");

	while (true);
	return 0;
}
 
