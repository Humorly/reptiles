// reptile.cpp: 定义控制台应用程序的入口点。
//

#include "pch.h"

#include <memory>
#include "spider.h"

int main()
{
	std::string str_net_;
	std::cout << "请输入网址:\n";
	std::cin >> str_net_;

	if (std::string::npos != str_net_.find("https"))
	{
		std::shared_ptr<spider> spider_https_(new https_spider());
		spider_https_->get_content(str_net_);
	}
	else if (std::string::npos != str_net_.find("http"))
	{
		std::shared_ptr<spider> spider_http_(new http_spider());
		spider_http_->get_content(str_net_);
	}

	while (true);
	return 0;
}
