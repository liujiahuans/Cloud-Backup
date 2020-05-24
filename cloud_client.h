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

class FileUtil {//�ļ�������
public:
	static bool Read(const std::string &name, std::string *body) {//���ļ��ж�ȡ��������
		std::ifstream fin(name, std::ios::binary);//�����ļ���

		if (!fin.is_open()) {
			std::cout << name << " is open failed!" << std::endl;
			return false;
		}

		int64_t fsize = boost::filesystem::file_size(name);//��ȡ�ļ���С
		body->resize(fsize);//����ռ� �����ļ�����
		fin.read(&(*body)[0], fsize);
		if (fin.good() == false) {
			std::cout << name << " read data failed!" << std::endl;
			return false;
		}

		fin.close();
		return true;
	}

	static bool Write(const std::string &name, const std::string &body) {//���ļ���д������
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
	
	bool Insert(const std::string &key, const std::string &val) {//����֮�����/����������Ϣ
		_backup_list[key] = val;
		Storage();
		return true;
	}

	bool GetEtag(const std::string &key, std::string *val) {//��ȡԭ�ļ�etag��Ϣ���жԱ�
		auto it = _backup_list.find(key);
		if (it == _backup_list.end()) {
			return false;
		}
		*val = it->second;
		return true;
	}

	bool InitLoad() {//��ʼ������ԭ������
		std::string body;
		if (FileUtil::Read(_store_file, &body) == false) {//��ȡ�ļ�����
			return false;
		}
		//��boost�⺯�������ַ����ķָ��
		std::vector<std::string> list;
		boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
		//ÿһ�а��տո�key��val���зָ�
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

	bool Storage() {//����������Ϣ�󣬳־û��洢
		//�����ݶ������л�
		//���ģ��ֻ�����ļ����Ĵ洢�������ļ��������
		//�Ƚ��ļ���д��string stream��
		std::stringstream tmp;//ʵ����һ��string������
		for (auto it = _backup_list.begin(); it != _backup_list.end(); ++it) {
			tmp << it->first << ' ' << it->second << "\r\n";
		}
		//�ٵ����ļ������ཫ�ļ����ַ���д��
		FileUtil::Write(_store_file, tmp.str());//��Ϊ�ļ������಻Ӱ���̰߳�ȫ�����Բ��ü�д��
		return true;
	}
private:
	std::string _store_file;//�־û��洢�ļ�����
	std::unordered_map<std::string, std::string> _backup_list;//�����ļ�����Ϣ�������ļ���etag��Ϣ
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

	bool Start() {//���������ļ���������
		data_manage.InitLoad();//��ʼ�������ļ���Ϣ
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

	bool GetBackupFileList(std::vector<std::string> *list){//��ȡ��Ҫ���ݵ��ļ��б�
		if (boost::filesystem::exists(_listen_dir) == false) {
			boost::filesystem::create_directory(_listen_dir);
		}
		boost::filesystem::directory_iterator begin(_listen_dir);//��ȡ��ǰĿ¼���ļ���Ϣ
		boost::filesystem::directory_iterator end;
		for (; begin != end; ++begin) {
			if (boost::filesystem::is_directory(begin->status())) {//����Ҫ����Ŀ¼
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

	bool GetEtag(const std::string &pathname, std::string *etag){//�����ļ�etag��Ϣ
		int64_t fsize = boost::filesystem::file_size(pathname);
		time_t mtime = boost::filesystem::last_write_time(pathname);
		*etag = std::to_string(fsize) + '-' + std::to_string(mtime);
		return true;
	}
private:
	std::string _listen_dir;//���Ŀ¼
	DataManager data_manage;
	std::string _ser_ip;
	uint16_t _ser_port;
};