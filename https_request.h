
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/bind.hpp>
#include <iostream>
#include <istream>
#include <ostream>
#include <string>

#include "const_value.h"

namespace wcont
{
	using boost::asio::deadline_timer;


	// ҳ����Ϣ
	class net_message
	{
	public:
		enum {
			type_net = 0,
			type_file = 1,
		};
		net_message() : str_base_net_(""), str_net_(""), type_(0) {}

		// ���񵽵�ҳ����Ϣ
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
			// ֹͣ���
			stop_ = false;
			// ���浱ǰҳ������
			msg_ = msg;
			// ����ҳ��
			std::string str_base_ = (std::string::npos == msg_.get_net_str().find(".com"))
				? msg_.get_base_net_str() + msg_.get_net_str() : msg_.get_net_str();

			// �ж���http����https

			if (std::string::npos != str_base_.find(".com"))
			{
				std::size_t start_pos_ = std::string::npos != str_base_.find("http://") ? str_base_.find("http://") + strlen("http://")
					: std::string::npos != str_base_.find("https://") ? str_base_.find("https://") + strlen("https://") : 0;
				std::size_t end_pos_ = std::string::npos != str_base_.find(".com") ? str_base_.find(".com") + strlen(".com") : 0;

				// ��ȡ��ַ
				url_ = std::string(str_base_.begin() + start_pos_, str_base_.begin() + end_pos_);
				// �ü�·��
				path_ = std::string(str_base_.begin() + end_pos_, str_base_.end());

				boost::asio::ip::tcp::resolver resolver_(io_service_);
				boost::asio::ip::tcp::resolver::query query_(url_, "443");
				boost::asio::ip::tcp::resolver::iterator resolver_iter_ = resolver_.resolve(query_);

				context_.reset(new boost::asio::ssl::context(boost::asio::ssl::context::sslv23));
				// ����socket
				socket_.reset(new boost::asio::ssl::stream<boost::asio::ip::tcp::socket>(io_service_, *context_));
				// ���ó�ʱ
				deadline_.reset(new deadline_timer(io_service_));

				socket_->set_verify_mode(boost::asio::ssl::context::verify_none);
				socket_->set_verify_callback(&verify_certificate);

				boost::asio::async_connect(socket_->lowest_layer(), resolver_iter_,
					[this](const boost::system::error_code& ec, boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
					handle_connect(url_, path_, ec);
				});

				// �󶨳�ʱ
				deadline_->async_wait(boost::bind(&https_request::check_deadline, this));

				// ����
				io_service_.run();
			}
		}

		void handle_connect(const std::string & url, const std::string & path, const boost::system::error_code& error)
		{
			if (!error) {
				socket_->async_handshake(boost::asio::ssl::stream_base::client,
					boost::bind(&https_request::handle_handshake, this, 
						url, path, boost::asio::placeholders::error));
			}
			else {
				std::cout << "Connect failed: " << error.message() << std::endl;
			}
		}

		void handle_handshake(const std::string & url, const std::string & path, const boost::system::error_code& error)
		{
			if (!error) 
			{
				std::stringstream request_;

				request_ << "GET " << path << " HTTP/1.1\r\n";
				request_ << "Host:" << url << "\r\n";
				request_ << "Accept: */*\r\n";
				request_ << "\r\n";

				boost::asio::async_write(*socket_, boost::asio::buffer(request_.str()),
					boost::bind(&https_request::handle_write, this, boost::asio::placeholders::error,
						boost::asio::placeholders::bytes_transferred));
			}
			else 
			{
				std::cout << "Handshake failed: " << error.message() << std::endl;
				// �ر��˳�
				close();
			}
		}

		void handle_write(const boost::system::error_code& error, size_t bytes_transferred)
		{
			if (!error) 
			{
				//std::cout << "Sending request OK!" << std::endl;
				boost::asio::async_read_until(*socket_, reply_, "\r\n",
					[this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
						// ����״̬
						read_status_code(ec);
				});
			}
			else 
			{
				std::cout << "Write failed: " << error.message() << std::endl;
			}
		}

		// ��������״̬
		void read_status_code(const boost::system::error_code& err)
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

					// �رն�ȡ
					close();

					return;
				}
				if (valid_http_ret_val != status_code_)
				{
					std::cerr << "Response returned with status code ";
					std::cerr << status_code_ << "\n";

					// �رն�ȡ
					close();

					return;
				}

				boost::asio::async_read_until(*socket_, reply_, "\r\n\r\n",
					[this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
						read_headers(ec);
				});
			}
			else
			{
				std::cerr << "Error: " << err << "\n";
			}
		}

		// ��ȡҳ��ͷ��
		void read_headers(const boost::system::error_code& err)
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

				boost::asio::async_read(*socket_, reply_,
					boost::asio::transfer_at_least(1),
					[this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
					read_content(ec);
				});
			}
			else
			{
				std::cerr << "Error: " << err << "\n";
			}
		}

		void read_content(const boost::system::error_code& err)
		{
			// ���ö�ȡ��ʱ
			deadline_->expires_from_now(boost::posix_time::millisec(100));

			//std::cerr << "read_content..."  "\n";
			if (err)
			{
				message_body_ << &reply_;
			}
			else if (!err)
			{
				message_body_ << &reply_;
				reply_.consume(reply_.size()); 

				boost::asio::async_read(*socket_, reply_,
					boost::asio::transfer_at_least(1),
					[this](const boost::system::error_code& ec, std::size_t bytes_transferred) {
					read_content(ec);
				});
			}
		}

		// ��鳬ʱ
		void check_deadline()
		{
			if (deadline_->expires_at() <= deadline_timer::traits_type::now())
			{
				// �洢����
				save_content();
				deadline_->expires_at(boost::posix_time::pos_infin);
			}

			deadline_->async_wait(boost::bind(&https_request::check_deadline, this));
		}

		// ��������
		void save_content()
		{
			// �����������
			message_body_ << &reply_;
			//std::cout << message_body_.str();

			if (msg_.get_type())
			{

				// �ü��ļ�����
				auto npos_ = path_.rfind('/');
				std::string str_file_;

				if (std::string::npos != npos_)
				{
					// �����ļ�����
					str_file_ = std::move(std::string(path_.begin() + npos_ + 1, path_.end()));
				}

				// д���ļ�
				try
				{
					std::cout << "���ڱ����ļ�:" << str_file_ << std::endl;

					std::ofstream file_(str_file_, std::ios::out | std::ios_base::binary | std::ios::trunc);
					if (file_.is_open())
					{
						// д������
						file_ << message_body_.str();
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
			}
			else
			{
				// ������ַ
				std::string net_content_ = message_body_.str();
				boost::regex net_regex_("href=\"([//]{0,})([^\"]*?)\"");
				boost::smatch net_smatch_;
				// ����ƥ������
				while (boost::regex_search(net_content_, net_smatch_, net_regex_))
				{
					// ������ַ������
					if (!net_smatch_.empty())
					{
						// ��ȡ��ַ
						std::string str_net_ = net_smatch_[2].str();

						// ��ӵ��б�֮��
						// ���´�����֮��						
						map_analy_.insert(std::make_pair<std::size_t, wcont::net_message>(1, net_message(msg_.get_base_net_str(), str_net_, 0)));
					}

					// ��������
					net_content_ = net_smatch_.suffix();
				}

				// �����ļ�
				std::string src_content_ = message_body_.str();
				boost::regex src_regex_("src=\"([//]{0,})([^\"]*?)\"");
				boost::smatch src_smatch_;
				// ����ƥ������
				while (boost::regex_search(src_content_, src_smatch_, src_regex_))
				{
					// ����·��
					if (!src_smatch_.empty())
					{
						// ��ȡ�ļ�·��
						std::string str_src_ = src_smatch_[2].str();
						// ��ӵ��б�֮��
						// ���´�����֮��						
						map_analy_.insert(std::make_pair<std::size_t, wcont::net_message>(0, net_message(msg_.get_base_net_str(), str_src_, 1)));

					}
					// ��������
					src_content_ = src_smatch_.suffix();
				}
			}

			// �رն�ȡ
			close();
		}

		// �رն�ȡ
		void close()
		{
			try
			{
				// ֹͣ����
				io_service_.stop();
				socket_->shutdown();
			}
			catch (const std::exception&)
			{
				// ��������
			}
			// ֹͣ����
			stop_ = true;
		}

		// ֤��У�� --> ��û�з����κ�ʵ��֤��
		static bool verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx)
		{
			//std::cerr << "verify_certificate "  "\n";

			char subject_name_[256];
			X509 * cert_ = X509_STORE_CTX_get_current_cert(ctx.native_handle());
			X509_NAME_oneline(X509_get_subject_name(cert_), subject_name_, 256);
			//std::cerr << "Verifying " << subject_name_ << "  " << preverified << "\n";

			return preverified;
		}

		// ��ǰ���״̬
		bool stoped() { return stop_.load(); }

	private:
		boost::asio::io_service io_service_;
		std::shared_ptr<boost::asio::ssl::context> context_;
		// ͷ����Ϣ
		std::ostringstream message_header_;
		// ����
		std::ostringstream message_body_;

		// ������
		std::size_t status_code_;
		// ������
		std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket_;
		boost::asio::streambuf reply_;

		// ��ʱ����
		std::shared_ptr<deadline_timer> deadline_;

		// ����ҳ��
		std::string url_;
		// ����·��
		std::string path_;

		// ��ǰ����ҳ������
		net_message msg_;

		// ֹͣ���
		std::atomic_bool stop_;

		// �������Ľ��
		std::multimap<std::size_t, wcont::net_message> & map_analy_;
	};
}

