
#include <asio.hpp>
#include <asio/ssl.hpp>

#include <iostream>
#include <istream>
#include <ostream>
#include <string>
#include <filesystem>

#include "const_value.h"

namespace wcont
{
	using asio::high_resolution_timer;

	// 页面信息
	class net_message
	{
	public:
		enum {
			type_net = 0,
			type_file = 1,
		};
		net_message() : str_base_net_(""), str_net_(""), type_(0) {}

		// 捕获到的页面信息
		net_message(std::string str_base_net, std::string str_net, std::size_t type) 
			: str_base_net_(str_base_net), str_net_(str_net), type_(type) {}
		virtual ~net_message() {}

		std::string get_base_net_str() { return str_base_net_; }
		std::string get_net_str() { return str_net_; }
		std::size_t get_type() { return type_; }

	private:
		std::string str_base_net_;
		std::string str_net_;
		std::size_t type_;
	};

	class https_request
	{
	public:
		https_request(const net_message & msg, std::multimap<std::size_t, wcont::net_message> & ma_analy) 
			: map_analy_(ma_analy)
		{
			// 停止标记
			stop_ = false;
			// 保存当前页面类型
			msg_ = msg;
			// 重置页面
			std::string str_base_ = (std::string::npos == msg_.get_net_str().find(".com"))
				? msg_.get_base_net_str() + msg_.get_net_str() : msg_.get_net_str();

			// 判定是http还是https
			if (std::string::npos != str_base_.find(".com"))
			{
				std::size_t start_pos_ = std::string::npos != str_base_.find("http://") ? str_base_.find("http://") + strlen("http://")
					: std::string::npos != str_base_.find("https://") ? str_base_.find("https://") + strlen("https://") : 0;
				std::size_t end_pos_ = std::string::npos != str_base_.find(".com") ? str_base_.find(".com") + strlen(".com") : 0;

				// 错误页面
				if (start_pos_ > end_pos_)
				{
					// 停止接收
					stop_ = true;
					return;
				}

				// 获取网址
				url_ = std::string(str_base_.begin() + start_pos_, str_base_.begin() + end_pos_);
				// 裁剪路径
				path_ = std::string(str_base_.begin() + end_pos_, str_base_.end());

				asio::ip::tcp::resolver resolver_(io_service_);
				asio::ip::tcp::resolver::query query_(url_, "443");
				asio::ip::tcp::resolver::iterator resolver_iter_ = resolver_.resolve(query_);

				context_.reset(new asio::ssl::context(asio::ssl::context::sslv23));
				// 不使用证书
				//context_->load_verify_file("ca.pem");

				// 重置socket
				socket_.reset(new asio::ssl::stream<asio::ip::tcp::socket>(io_service_, *context_));
				// 重置超时
				timer_.reset(new asio::high_resolution_timer(io_service_));

				// 不使用证书
				//socket_->set_verify_mode(asio::ssl::context::verify_peer);
				socket_->set_verify_mode(asio::ssl::context::verify_none);
				socket_->set_verify_callback(&verify_certificate);

				asio::async_connect(socket_->lowest_layer(), resolver_iter_,
					[this](const std::error_code& ec, asio::ip::tcp::resolver::iterator endpoint_iterator) {
					handle_connect(url_, path_, ec);
				});

				// 绑定超时	
				timer_->expires_from_now(std::chrono::seconds(10));
				timer_->async_wait(std::bind(&https_request::check_deadline, this));

				// 启动
				io_service_.run();
			}
		}

		void handle_connect(const std::string & url, const std::string & path, const std::error_code& error)
		{
			if (!error) {
				socket_->async_handshake(asio::ssl::stream_base::client,
					std::bind(&https_request::handle_handshake, this, 
						url, path, std::placeholders::_1));
			}
			else {
				std::cout << "Connect failed: " << error.message() << std::endl;

				// 关闭连接
				close();
			}
		}

		void handle_handshake(const std::string & url, const std::string & path, const std::error_code& error)
		{
			if (!error) 
			{
				std::stringstream request_;

				request_ << "GET " << path << " HTTP/1.1\r\n";
				request_ << "Host:" << url << "\r\n";
				request_ << "User-Agent:Mozilla/5.0 (Windows; U; Windows NT 6.1; en-US; rv:1.9.1.6) Gecko/20091201 Firefox/3.5.6";
				request_ << "Accept: */*\r\n";
				request_ << "\r\n";

				asio::async_write(*socket_, asio::buffer(request_.str()),
					std::bind(&https_request::handle_write, this, std::placeholders::_1,
						std::placeholders::_2));
			}
			else 
			{
				std::cout << "Handshake failed: " << error.message() << std::endl;
				// 关闭退出
				close();
			}
		}

		void handle_write(const std::error_code& error, size_t bytes_transferred)
		{
			if (!error) 
			{
				//std::cout << "Sending request OK!" << std::endl;
				asio::async_read_until(*socket_, reply_, "\r\n",
					[this](const std::error_code& ec, std::size_t bytes_transferred) {
						// 解析状态
						read_status_code(ec);
				});
			}
			else 
			{
				std::cout << "Write failed: " << error.message() << std::endl;

				// 关闭连接
				close();
			}
		}

		// 解析返回状态
		void read_status_code(const std::error_code& err)
		{
			//std::cerr << "read status..."  "\n";
			if (!err)
			{
				std::istream response_stream_(&reply_);
				std::string http_version_;
				response_stream_ >> http_version_;
				response_stream_ >> status_code_;
				std::string status_message_;
				std::getline(response_stream_, status_message_);
				if (!response_stream_ || http_version_.substr(0, 5) != "HTTP/")
				{
					std::cerr << "Invalid response\n";

					// 关闭读取
					close();

					return;
				}
				if (valid_http_ret_val != status_code_)
				{
					std::cerr << "Response returned with status code ";
					std::cerr << status_code_ << "\n";

					// 关闭读取
					close();

					return;
				}

				asio::async_read_until(*socket_, reply_, "\r\n\r\n",
					[this](const std::error_code& ec, std::size_t bytes_transferred) {
						read_headers(ec);
				});
			}
			else
			{
				std::cerr << "Error: " << err << "\n";

				// 关闭读取
				close();
			}
		}

		// 读取页面头部
		void read_headers(const std::error_code& err)
		{
			//std::cerr << "read headers..."  "\n";
			if (!err)
			{
				std::istream response_stream_(&reply_);
				std::string header_;
				while (std::getline(response_stream_, header_) && header_ != "\r")
				{
					message_header_ << header_ << "\n";
				}

				asio::async_read(*socket_, reply_,
					asio::transfer_at_least(1),
					[this](const std::error_code& ec, std::size_t bytes_transferred) {
					read_content(ec);
				});
			}
			else
			{
				std::cerr << "Error: " << err << "\n";

				// 关闭读取
				close();
			}
		}

		void read_content(const std::error_code& err)
		{
			std::cerr << "read_content..."  "\n";
			if (err)
			{
				//message_body_ << &reply_;
				save_content();
			}
			else if (!err)
			{
				message_body_ << &reply_;
				reply_.consume(reply_.size()); 

				asio::async_read_until(*socket_, reply_,
					"\r\n",
					[this](const std::error_code& ec, std::size_t bytes_transferred) {
					read_content(ec);
				});
			}
		}

		// 检查超时
		void check_deadline()
		{
			// 存储内容
			save_content();
			timer_->expires_from_now(std::chrono::seconds(10));
			timer_->async_wait(std::bind(&https_request::check_deadline, this));
		}

		// 保存内容
		void save_content()
		{
			// 输出最终内容
			message_body_ << &reply_;
			//std::cout << message_body_.str();

			if (msg_.get_type())
			{

				// 裁剪文件名称
				auto npos_ = path_.rfind('/');
				std::string str_file_;

				if (std::string::npos != npos_)
				{
					// 拷贝文件名称
					str_file_ = std::move(std::string("res/") + std::string(path_.begin() + npos_ + 1, path_.end()));
					std::filesystem::create_directory(std::string("res"));
				}

				// 写入文件
				try
				{
					std::cout << "正在保存文件:" << str_file_ << std::endl;

					std::ofstream file_(str_file_, std::ios::out | std::ios_base::binary | std::ios::trunc);
					if (file_.is_open())
					{
						// 写入内容
						file_ << message_body_.str();
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
			}
			else
			{
				// 搜索网址
				std::string net_content_ = message_body_.str();
				std::regex net_regex_("href=\"([//]{0,})([^\"]*?)\"");
				std::smatch net_smatch_;
				// 搜索匹配内容
				while (std::regex_search(net_content_, net_smatch_, net_regex_))
				{
					// 捕获网址与名称
					if (!net_smatch_.empty())
					{
						// 获取网址
						std::string str_net_ = net_smatch_[2].str();

						// 添加到列表之中
						// 供下次搜索之用						
						map_analy_.insert(std::make_pair<std::size_t, wcont::net_message>(1, net_message(msg_.get_base_net_str(), str_net_, 0)));
					}

					// 继续搜索
					net_content_ = net_smatch_.suffix();
				}

				// 搜索文件
				std::string src_content_ = message_body_.str();
				std::regex src_regex_("src=\"([//]{0,})([^\"]*?)\"");
				std::smatch src_smatch_;
				// 搜索匹配内容
				while (std::regex_search(src_content_, src_smatch_, src_regex_))
				{
					// 捕获路径
					if (!src_smatch_.empty())
					{
						// 获取文件路径
						std::string str_src_ = src_smatch_[2].str();
						// 添加到列表之中
						// 供下次搜索之用						
						map_analy_.insert(std::make_pair<std::size_t, wcont::net_message>(0, net_message(msg_.get_base_net_str(), str_src_, 1)));
					}
					// 继续搜索
					src_content_ = src_smatch_.suffix();
				}
			}

			// 关闭读取
			close();
		}

		// 关闭读取
		void close()
		{
			try
			{
				// 停止发送
				io_service_.stop();
				socket_->shutdown();
			}
			catch (const std::exception&)
			{
				// 不做处理
			}
			// 停止接收
			stop_ = true;
		}

		// 证书校验 --> 并没有发送任何实质证书
		static bool verify_certificate(bool preverified, asio::ssl::verify_context& ctx)
		{
			//std::cerr << "verify_certificate "  "\n";

			char subject_name_[256];
			X509 * cert_ = X509_STORE_CTX_get_current_cert(ctx.native_handle());
			X509_NAME_oneline(X509_get_subject_name(cert_), subject_name_, 256);
			//std::cerr << "Verifying " << subject_name_ << "  " << preverified << "\n";

			return preverified;
		}

		// 当前完成状态
		bool stoped() { return stop_.load(); }

	private:
		asio::io_context io_service_;
		std::shared_ptr<asio::ssl::context> context_;
		// 头部信息
		std::ostringstream message_header_;
		// 内容
		std::ostringstream message_body_;

		// 返回码
		std::size_t status_code_;
		// 数据流
		std::shared_ptr<asio::ssl::stream<asio::ip::tcp::socket>> socket_;
		asio::streambuf reply_;

		// 超时处理
		std::shared_ptr<asio::high_resolution_timer> timer_;

		// 访问页面
		std::string url_;
		// 访问路径
		std::string path_;

		// 当前解析页面类型
		net_message msg_;

		// 停止标记
		std::atomic_bool stop_;

		// 分析出的结果
		std::multimap<std::size_t, wcont::net_message> & map_analy_;
	};
}

