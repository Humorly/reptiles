#pragma once

#ifndef __H_HTTP_REQUEST_H__
#define __H_HTTP_REQUEST_H__

#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <fstream>
#include <unordered_map>

#include <boost/asio.hpp>
// 导入正则
#include <boost/regex.hpp>

#include "const_value.h"

namespace wcont
{
	// post get request to server
	using boost::asio::ip::tcp;

	class http_request
	{
	public:
		// 枚举返回值

		http_request() {}
		virtual ~http_request() {}

		// 设置基准页面
		void set_base_link(const std::string str_link) { str_base_link_ = std::move(str_link); }
			
		// 获取页面内容
		std::size_t get_content(const std::string & link, const std::unordered_map<std::string, std::string> & map_net, 
			std::unordered_map<std::string, std::string> & map_analy)
		{
			// 读取内容
			read_content(link);

			// 搜索网址
			std::string net_content_ = ostream_content_.str();
			boost::regex net_regex_("href=\"([^\"]*?)\"");
			boost::smatch net_smatch_;
			// 搜索匹配内容
			while (boost::regex_search(net_content_, net_smatch_, net_regex_))
			{
				// 捕获网址与名称
				if (!net_smatch_.empty())
				{
					// 获取网址
					std::string str_net_ = net_smatch_[1].str();

					// 添加到列表之中
					// 供下次搜索之用
					if (map_net.end() == map_net.find(str_net_))
						map_analy[str_net_] = str_net_;
				}

				// 继续搜索
				net_content_ = net_smatch_.suffix();
			}

			// 搜索文件
			std::string src_content_ = ostream_content_.str();
			boost::regex src_regex_("src=\"([^\"]*?)\"");
			boost::smatch src_smatch_;
			// 搜索匹配内容
			while (boost::regex_search(src_content_, src_smatch_, src_regex_))
			{
				// 捕获路径
				if (!src_smatch_.empty())
				{
					// 获取文件路径
					std::string str_src_ = src_smatch_[1].str();

					// 下载文件
					get_download(str_src_);
				}

				// 继续搜索
				src_content_ = src_smatch_.suffix();
			}

			return 0;
		}

		// 下载文件
		std::size_t get_download(const std::string & link)
		{
			// 裁剪文件名称
			auto npos_ = link.rfind('/');
			std::string str_file_;

			if (std::string::npos != npos_)
			{
				// 拷贝文件名称
				str_file_ = std::move(std::string(link.begin() + npos_ + 1, link.end()));
			}

			// 读取内容
			read_content(link);

			// 写入文件
			try
			{
				std::ofstream file_(str_file_, std::ios::out | std::ios_base::binary | std::ios::trunc);
				if (file_.is_open())
				{
					// 写入内容
					file_ << ostream_content_.str();
					// 关闭文件
					file_.close();
				}
				else
				{
					throw std::string("Can't open the file!");
				}
			}
			catch (...)
			{
				std::cout << "Can't save file " << str_file_ << std::endl;
			}

			return 0;
		}

	private:

		// 读取页面内容
		std::size_t read_content(const std::string & link)
		{
			try
			{
				// 重置页面
				std::string str_base_;

				// 如果是子页加上基础网址
				str_base_ = (std::string::npos == link.find(".com")) ? str_base_link_ + link : link;

				// 判定是http还是https
				std::size_t start_pos_ = std::string::npos != str_base_.find("http://") ? str_base_.find("http://") + strlen("http://")
					: std::string::npos != str_base_.find("https://") ? str_base_.find("https://") + strlen("https://") : 0;
				std::size_t end_pos_ = std::string::npos != str_base_.find(".com") ? str_base_.find(".com") + strlen(".com") : 0;

				// 获取网址
				std::string url(str_base_.begin() + start_pos_, str_base_.begin() + end_pos_);
				// 裁剪路径
				std::string path(str_base_.begin() + end_pos_, str_base_.end());

				// 创建服务
				boost::asio::io_service io_service_;

				// 创建指向服务器的解析器
				tcp::resolver resolver_(io_service_);
				tcp::resolver::query query_(url, "http");
				tcp::resolver::iterator endpoint_iterator_ = resolver_.resolve(query_);

				// 绑定socket
				tcp::socket socket_(io_service_);
				// 阻塞连接之
				boost::asio::connect(socket_, endpoint_iterator_);

				// 准备请求
				boost::asio::streambuf request_;
				std::ostream request_stream_(&request_);
				request_stream_ << "GET " << path << " HTTP/1.0\r\n";
				request_stream_ << "Host: " << url << "\r\n";
				request_stream_ << "Accept: */*\r\n";
				request_stream_ << "Connection: close\r\n\r\n";

				// 丢出请求
				boost::asio::write(socket_, request_);

				// 读取返回的头部信息
				boost::asio::streambuf response_;
				boost::asio::read_until(socket_, response_, "\r\n");

				// 检查返回结果
				std::istream response_stream_(&response_);
				std::string http_version_;
				response_stream_ >> http_version_;
				std::size_t status_code_;
				response_stream_ >> status_code_;
				std::string status_message_;
				std::getline(response_stream_, status_message_);
				if (!response_stream_ || "HTTP/" != http_version_.substr(0, 5))
				{
					std::cout << "Invalid response\n";
					return 1;
				}
				// 是否有效返回
				if (valid_http_ret_val != status_code_)
				{
					std::cout << "Response returned with status code " << status_code_ << "\n";
					return 1;
				}

				// 读取结束头部
				boost::asio::read_until(socket_, response_, "\r\n\r\n");

				// 跳过头部信息
				std::string header_;
				while (std::getline(response_stream_, header_) && "\r" != header_);

				// 内容存储
				std::ostringstream ostream_;

				// 写入内容
				if (0 < response_.size())
					ostream_ << &response_;

				// 读取正文
				boost::system::error_code error_;
				while (boost::asio::read(socket_, response_,
					boost::asio::transfer_at_least(1), error_))
					ostream_ << &response_;

				// 抛出错误
				if (error_ != boost::asio::error::eof)
					throw boost::system::system_error(error_);

				// 存储解析内容
				ostream_content_ = std::move(ostream_);
			}
			// 捕获接收异常
			catch (const std::exception & e)
			{
				std::cout << "Exception: " << e.what() << "\n";
			}
			// 捕获其他异常
			catch (const std::string & e)
			{
				std::cout << "Exception: " << e << "\n";
			}

			return 0;
		}

		// 存储读取内容
		std::ostringstream ostream_content_;
		// 基准页面
		std::string str_base_link_;
	};
}

#endif // !__H_HTTP_REQUEST_H__