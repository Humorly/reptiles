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
// ��������
#include <boost/regex.hpp>

#include "const_value.h"

namespace wcont
{
	// post get request to server
	using boost::asio::ip::tcp;

	class http_request
	{
	public:
		// ö�ٷ���ֵ

		http_request() {}
		virtual ~http_request() {}

		// ���û�׼ҳ��
		void set_base_link(const std::string str_link) { str_base_link_ = std::move(str_link); }
			
		// ��ȡҳ������
		std::size_t get_content(const std::string & link, const std::unordered_map<std::string, std::string> & map_net, 
			std::unordered_map<std::string, std::string> & map_analy)
		{
			// ��ȡ����
			read_content(link);

			// ������ַ
			std::string net_content_ = ostream_content_.str();
			boost::regex net_regex_("href=\"([^\"]*?)\"");
			boost::smatch net_smatch_;
			// ����ƥ������
			while (boost::regex_search(net_content_, net_smatch_, net_regex_))
			{
				// ������ַ������
				if (!net_smatch_.empty())
				{
					// ��ȡ��ַ
					std::string str_net_ = net_smatch_[1].str();

					// ��ӵ��б�֮��
					// ���´�����֮��
					if (map_net.end() == map_net.find(str_net_))
						map_analy[str_net_] = str_net_;
				}

				// ��������
				net_content_ = net_smatch_.suffix();
			}

			// �����ļ�
			std::string src_content_ = ostream_content_.str();
			boost::regex src_regex_("src=\"([^\"]*?)\"");
			boost::smatch src_smatch_;
			// ����ƥ������
			while (boost::regex_search(src_content_, src_smatch_, src_regex_))
			{
				// ����·��
				if (!src_smatch_.empty())
				{
					// ��ȡ�ļ�·��
					std::string str_src_ = src_smatch_[1].str();

					// �����ļ�
					get_download(str_src_);
				}

				// ��������
				src_content_ = src_smatch_.suffix();
			}

			return 0;
		}

		// �����ļ�
		std::size_t get_download(const std::string & link)
		{
			// �ü��ļ�����
			auto npos_ = link.rfind('/');
			std::string str_file_;

			if (std::string::npos != npos_)
			{
				// �����ļ�����
				str_file_ = std::move(std::string(link.begin() + npos_ + 1, link.end()));
			}

			// ��ȡ����
			read_content(link);

			// д���ļ�
			try
			{
				std::ofstream file_(str_file_, std::ios::out | std::ios_base::binary | std::ios::trunc);
				if (file_.is_open())
				{
					// д������
					file_ << ostream_content_.str();
					// �ر��ļ�
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

		// ��ȡҳ������
		std::size_t read_content(const std::string & link)
		{
			try
			{
				// ����ҳ��
				std::string str_base_;

				// �������ҳ���ϻ�����ַ
				str_base_ = (std::string::npos == link.find(".com")) ? str_base_link_ + link : link;

				// �ж���http����https
				std::size_t start_pos_ = std::string::npos != str_base_.find("http://") ? str_base_.find("http://") + strlen("http://")
					: std::string::npos != str_base_.find("https://") ? str_base_.find("https://") + strlen("https://") : 0;
				std::size_t end_pos_ = std::string::npos != str_base_.find(".com") ? str_base_.find(".com") + strlen(".com") : 0;

				// ��ȡ��ַ
				std::string url(str_base_.begin() + start_pos_, str_base_.begin() + end_pos_);
				// �ü�·��
				std::string path(str_base_.begin() + end_pos_, str_base_.end());

				// ��������
				boost::asio::io_service io_service_;

				// ����ָ��������Ľ�����
				tcp::resolver resolver_(io_service_);
				tcp::resolver::query query_(url, "http");
				tcp::resolver::iterator endpoint_iterator_ = resolver_.resolve(query_);

				// ��socket
				tcp::socket socket_(io_service_);
				// ��������֮
				boost::asio::connect(socket_, endpoint_iterator_);

				// ׼������
				boost::asio::streambuf request_;
				std::ostream request_stream_(&request_);
				request_stream_ << "GET " << path << " HTTP/1.0\r\n";
				request_stream_ << "Host: " << url << "\r\n";
				request_stream_ << "Accept: */*\r\n";
				request_stream_ << "Connection: close\r\n\r\n";

				// ��������
				boost::asio::write(socket_, request_);

				// ��ȡ���ص�ͷ����Ϣ
				boost::asio::streambuf response_;
				boost::asio::read_until(socket_, response_, "\r\n");

				// ��鷵�ؽ��
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
				// �Ƿ���Ч����
				if (valid_http_ret_val != status_code_)
				{
					std::cout << "Response returned with status code " << status_code_ << "\n";
					return 1;
				}

				// ��ȡ����ͷ��
				boost::asio::read_until(socket_, response_, "\r\n\r\n");

				// ����ͷ����Ϣ
				std::string header_;
				while (std::getline(response_stream_, header_) && "\r" != header_);

				// ���ݴ洢
				std::ostringstream ostream_;

				// д������
				if (0 < response_.size())
					ostream_ << &response_;

				// ��ȡ����
				boost::system::error_code error_;
				while (boost::asio::read(socket_, response_,
					boost::asio::transfer_at_least(1), error_))
					ostream_ << &response_;

				// �׳�����
				if (error_ != boost::asio::error::eof)
					throw boost::system::system_error(error_);

				// �洢��������
				ostream_content_ = std::move(ostream_);
			}
			// ��������쳣
			catch (const std::exception & e)
			{
				std::cout << "Exception: " << e.what() << "\n";
			}
			// ���������쳣
			catch (const std::string & e)
			{
				std::cout << "Exception: " << e << "\n";
			}

			return 0;
		}

		// �洢��ȡ����
		std::ostringstream ostream_content_;
		// ��׼ҳ��
		std::string str_base_link_;
	};
}

#endif // !__H_HTTP_REQUEST_H__