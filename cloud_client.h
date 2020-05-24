#pragma once
#include<iostream>
#include<sstream>
#include<fstream>
#include<vector>
#include<string>
#include<unordered_map>
#include<boost/filesystem.hpp>
#include<boost/algorithm/string.hpp>
#include "httplib.h"

#define STORE_FILE "./list.backup"
#define LISTEN_DIR "./backup/"

class FileUtil {//文件工具类
public:
	static bool Read(const std::string &name, std::string *body) {//从文件中读取所有内容
		std::ifstream fin(name, std::ios::binary);//输入文件流

		if (!fin.is_open()) {
			std::cout << name << " is open failed!" << std::endl;
			return false;
		}

		int64_t fsize = boost::filesystem::file_size(name);//获取文件大小
		body->resize(fsize);//申请空间 接收文件数据
		fin.read(&(*body)[0], fsize);
		if (fin.good() == false) {
			std::cout << name << " read data failed!" << std::endl;
			return false;
		}

		fin.close();
		return true;
	}

	static bool Write(const std::string &name, const std::string &body) {//向文件中写入数据
		std::ofstream fout(name, std::ios::binary);
		if (fout.is_open() == false) {
			std::cout << name << " is open failed!" << std::endl;
			return false;
		}

		fout.write(&body[0], body.size());

		if (fout.good() == false) {
			std::cout << name << "is write failed!" << std::endl;
			return false;
		}
		fout.close();
		return true;
	}
};

class DataManager {
public:
	DataManager(const std::string filename) :
		_store_file(filename) {}
	
	bool Insert(const std::string &key, const std::string &val) {//备份之后插入/更新数据信息
		_backup_list[key] = val;
		Storage();
		return true;
	}

	bool GetEtag(const std::string &key, std::string *val) {//获取原文件etag信息进行对比
		auto it = _backup_list.find(key);
		if (it == _backup_list.end()) {
			return false;
		}
		*val = it->second;
		return true;
	}

	bool InitLoad() {//初始化加载原有数据
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false) {//读取文件数据
			return false;
		}
		//用boost库函数进行字符串的分割处理
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//每一行按照空格将key和val进行分割
		for (auto & i : list) {
			size_t pos = i.find(" ");
			if (pos == std::string::npos) {
				continue;
			}
			std::string key = i.substr(0, pos);
			std::string val = i.substr(pos + 1);
			Insert(key, val);
		}
		return true;
	}

	bool Storage() {//更新数据信息后，持久化存储
		//将数据对象序列化
		//这个模块只负责文件名的存储，不管文件里的数据
		//先将文件都写到string stream里
		std::stringstream tmp;//实例化一个string流对象
		for (auto it = _backup_list.begin(); it != _backup_list.end(); ++it) {
			tmp << it->first << ' ' << it->second << "\r\n";
		}
		//再调用文件工具类将文件流字符串写入
		FileUtil::Write(_store_file, tmp.str());//因为文件工具类不影响线程安全，所以不用加写锁
		return true;
	}
private:
	std::string _store_file;//持久化存储文件名称
	std::unordered_map<std::string, std::string> _backup_list;//备份文件的信息，包含文件和etag信息
};

class CloudClient {
public:
	CloudClient(const std::string &filename, const std::string &store_file, 
				const std::string &ser_ip, uint16_t ser_port) :
		_listen_dir(filename),
		data_manage(store_file),
		_ser_ip(ser_ip),
		_ser_port(ser_port)
		{ }

	bool Start() {//完成整体的文件备份流程
		data_manage.InitLoad();//初始化加载文件信息
		std::vector<std::string> list;
		GetBackupFileList(&list);
		while (true) {
			for (int i = 0; i < list.size(); ++i) {
				std::string pathname = _listen_dir + list[i];
				std::cout << pathname << " need to backup!\n";
				std::string body;
				FileUtil::Read(pathname, &body);
				httplib::Client client(_ser_ip, _ser_port);
				std::string req_path = '/' + list[i];
				auto res = client.Put(req_path.c_str(), body, "application/octet-stream");
				if (res == NULL || res->status != 200) {
					std::cout << pathname << " backup is failed!\n";
					continue;
				}

				std::string etag;
				GetEtag(pathname, &etag);
				data_manage.Insert(list[i], etag);
				std::cout << pathname << " backup is sucessful!\n";
			}
			Sleep(1000);
		}
		return true;
	}

	bool GetBackupFileList(std::vector<std::string> *list){//获取需要备份的文件列表
		if (boost::filesystem::exists(_listen_dir) == false) {
			boost::filesystem::create_directory(_listen_dir);
		}
		boost::filesystem::directory_iterator begin(_listen_dir);//获取当前目录下文件信息
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin) {
			if (boost::filesystem::is_directory(begin->status())) {//不需要备份目录
				continue;
			}
			std::string pathname = begin->path().string();
			std::string name = begin->path().filename().string();
			std::string cur_etag, etag;
			GetEtag(pathname, &cur_etag);
			data_manage.GetEtag(name, &etag);

			if (cur_etag != etag) {
				list->push_back(name);
			}
		}
		return true;
	}

	bool GetEtag(const std::string &pathname, std::string *etag){//计算文件etag信息
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + '-' + std::to_string(mtime);
		return true;
	}
private:
	std::string _listen_dir;//监控目录
	DataManager data_manage;
	std::string _ser_ip;
	uint16_t _ser_port;
};