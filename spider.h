#pragma once

#ifndef __H_SPIDER_H__
#define __H_SPIDER_H__

#include <string>
#include <unordered_map>
#include <map>

#include "http_request.h"
#include "https_request.h"

class spider
{
public:
	spider() {}
	virtual ~spider() {}

	virtual std::size_t get_content(std::string url) = 0;
};

class http_spider : public spider
{
public:
	http_spider() {}
	virtual ~http_spider() {}

	// 获取内容
	std::size_t get_content(std::string url) override
	{
		// 待爬的网站列表
		std::unordered_map<std::string, std::string> map_net_;
		map_net_[url] = url;

		// 分析出的结果
		std::unordered_map<std::string, std::string> map_analy_;

		wcont::http_request http_request_;
		// 设置基准页面
		http_request_.set_base_link(url);

		// 开始获取页面内容
		while (true)
		{
			// 分析需要爬的网站
			for (const auto & it : map_net_)
			{
				http_request_.get_content(it.first, map_net_, map_analy_);
			}

			// 分析完毕重置结果
			// 准备下一轮循环
			if (map_analy_.size())
			{
				// 设置下一轮要分析的内容
				map_net_ = map_analy_;
				// 清除之前分析页面得出的新页面
				map_analy_.clear();
			}
			else
			{
				std::cout << "No content to catch..." << std::endl;
				break;
			}
		}

		return valid_ret_val;
	}
};

class https_spider : public spider
{
public:
	https_spider() {}
	virtual ~https_spider() {}

	// 获取内容
	std::size_t get_content(std::string url) override
	{
		// 初始网址
		std::size_t end_pos_ = std::string::npos != url.find(".com") ? url.find(".com") + strlen(".com") : 0;
		std::string base_url_ = std::string(url.begin(), url.begin() + end_pos_);
		std::multimap<std::size_t, wcont::net_message> map_net_;
		map_net_.insert(std::make_pair<std::size_t, wcont::net_message>(1, wcont::net_message(base_url_, url, 0)));

		// 分析出的结果 
		std::multimap<std::size_t, wcont::net_message> map_analy_;

		try {
			// 开始获取页面内容
			while (true)
			{		
				// 分析需要爬的网站
				for (const auto & it : map_net_)
				{
					try
					{
						wcont::https_request https_request_(it.second, map_analy_);
						// 是否阻塞为佳？值得商榷
						while (!https_request_.stoped());
					}
					catch (...)
					{
						// 捕获异常
						std::cout << "Exception：Get content failed!" << std::endl;
					}
				}

				// 分析完毕重置结果
				// 准备下一轮循环
				if (map_analy_.size())
				{
					// 设置下一轮要分析的内容
					map_net_ = map_analy_;
					// 清除之前分析页面得出的新页面
					map_analy_.clear();
				}
				else
				{
					std::cout << "No content to catch..." << std::endl;
					break;
				}
			}
		}
		catch (std::exception& e) 
		{
			std::cerr << "Exception: " << e.what() << "\n";
		}

		return valid_ret_val;
	}
};

#endif // !__H_SPIDER_H__
