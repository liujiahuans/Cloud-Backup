#include<cstdio>
#include<iostream>
#include<string>
#include<vector>
#include<fstream>
#include<unordered_map>
#include<zlib.h>
#include<pthread.h>
#include<boost/filesystem.hpp>
#include<boost/algorithm/string.hpp>
#include "httplib.h"

#define NONHOT_TIME 10  //最后一次访问时间在10秒以外就是非热点文件
#define INTREVAL_TIME 30 //每隔30秒检测一次非热点
#define BACKUP_DIR "./backup/" //文件的备份路径
#define GZFILE_DIR "./gzfile/"  //压缩包存放的路径
#define GZ_SUF ".gz" //压缩包文件名后缀
#define DATA_FILE "./list.backup" //数据管理模块的数据备份文件名称

namespace _cloud_sys {

  class FileUtil {//文件工具类
    public:
      static bool Read (const std::string &name, std::string *body){//从文件中读取所有内容
        std::ifstream fin (name, std::ios::binary);//输入文件流

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

      static bool Write (const std::string &name, const std::string &body) {//向文件中写入数据
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
  class CompressUtil{//压缩工具类
    public:
      static bool Compress (const std::string &src, const std::string &dst){//文件压缩
        std::string body;
        FileUtil::Read(src, &body);

        gzFile gzf = gzopen(dst.c_str(), "wb");//打开压缩包
        if (gzf == NULL) {
          std::cout << dst << " is open failed!" << std::endl;
          return false;
        }

        int wlen = 0;
        while (wlen < body.size()) {
          int ret = gzwrite(gzf, &body[wlen], body.size() - wlen);
          if (ret == 0) {
            std::cout << dst << " write compress data failed!" << std::endl;
            return false; 
          }
          wlen += ret;
        }
        gzclose(gzf);
        return true;
      }

      static bool UnCompress (const std::string &src, const std::string &dst){//文件解压缩
        std::ofstream fout(dst, std::ios::binary);
        if (!fout.is_open()) {
          std::cout << dst << " is open failed!" << std::endl;
          return false;
        }

        gzFile gzf = gzopen (src.c_str(), "rb");
        if (gzf == NULL) {
          std::cout << src << " is open failed!" << std::endl;
          return false;
        }

        int ret = 0;
        char tmp[4096] = { 0 };
        while ((ret = gzread(gzf, tmp, 4096)) > 0) {
          fout.write(tmp, ret);
        }
        fout.close();
        gzclose(gzf);
        return true;
      }
  }; 
  class DataManager {//通信模块和压缩模块都有用到数据管理模块因此此模块需要实例化一个全局变量
    public:
      DataManager (const std::string &path) 
        :_back_file (path) {//文件的存储路径
          pthread_rwlock_init(&_rwlock, NULL);
        }

      ~DataManager () {
        pthread_rwlock_destroy(&_rwlock);
      }

      bool exists (const std::string &name) {//判断文件是否存在
        //file_list在多个模块都有使用，为了线程安全要加上读写锁
        pthread_rwlock_rdlock(&_rwlock);//只是为了获取一个文件的名称所以只需要加读锁就可以了
        auto it = _file_list.find(name);
        if (it == _file_list.end()) {
          pthread_rwlock_unlock(&_rwlock);
          return false;
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      }

      bool isCompress (const std::string &name) {//判断文件是否已经压缩
        pthread_rwlock_rdlock(&_rwlock); 
        auto it = _file_list.find(name);
        if (it == _file_list.end()) {
          pthread_rwlock_unlock(&_rwlock);
          return false;
        }

        if (it->first == it->second) {
          pthread_rwlock_unlock(&_rwlock);
          return false;
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      }

      bool nonCompressList(std::vector<std::string> *list) {//获取未压缩文件列表
        pthread_rwlock_rdlock(&_rwlock);
        for (auto it = _file_list.begin(); it != _file_list.end(); ++it) {
          if (it->first == it->second) {//如果first和second相等 说明文件没有压缩
            list->push_back(it->first);
          }
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      }

      bool insert(const std::string &src, const std::string &dst) {//插入/更新数据
        pthread_rwlock_wrlock(&_rwlock);//要修改原文件的名称 所以要加写锁
        _file_list[src] = dst;
        pthread_rwlock_unlock(&_rwlock);
        return true;
      }

      bool getAllName (std::vector<std::string> *list) {//获取所有文件名称
        pthread_rwlock_rdlock(&_rwlock);//获取名称加读锁
        for (auto it = _file_list.begin(); it != _file_list.end(); ++it) {
          list->push_back(it->first);//即使文件压缩后 用户看到的文件名称也是原文件的名称
        }
        pthread_rwlock_unlock(&_rwlock);
        return true;
      }

      bool getGzName (std::string &src, std::string *dst) {//根据原文件名称获取压缩包文件名称
        auto it = _file_list.find(src);
        if (it == _file_list.end()) {
          return false;
        }
        *dst = it->second;
        return true;
      }

      bool storage (){//数据改变后持久化存储
        //将数据对象序列化
        //这个模块只负责文件名的存储，不管文件里的数据
        //先将文件都写到string stream里
        std::stringstream tmp;//实例化一个string流对象
        pthread_rwlock_rdlock(&_rwlock);
        for (auto it = _file_list.begin(); it != _file_list.end(); ++it) {
          tmp << it->first << ' ' << it->second << "\r\n";
        }
        pthread_rwlock_unlock(&_rwlock);
        //再调用文件工具类将文件流字符串写入
        FileUtil::Write(_back_file, tmp.str());//因为文件工具类不影响线程安全，所以不用加写锁
        return true;
      }

      bool initLoad() {//启动时初始化加载原有数据
        std::string body;
        if (FileUtil::Read(_back_file ,&body) == false) {//读取文件数据
          return false;
        }
        //用boost库函数进行字符串的分割处理
        std::vector<std::string> list;
        boost::split(list, body, boost::is_any_of("\r\n"), boost::token_compress_off);
        //每一行按照空格将key和val进行分割
        for (auto & i : list) {
          size_t pos = i.find( " " );
          if (pos == std::string::npos) {
            continue;
          }
          std::string key = i.substr(0, pos);
          std::string val = i.substr(pos + 1);
          insert(key, val);
        }
        return true;
      }
    private:
      std::string _back_file;//持久化数据存储文件名称
      std::unordered_map<std::string, std::string> _file_list;//数据管理容器
      pthread_rwlock_t _rwlock;//读写锁
  };

  _cloud_sys:: DataManager data_manage(DATA_FILE);

  class NonHotCompress {//非热点压缩
    public:
      NonHotCompress (const std::string gz_dir_name, const std::string back_dir_name)
        :_gz_dir(gz_dir_name),
        _backup_dir(back_dir_name){}
      bool Start(){//总体向外提供的一个接口，开始压缩模块
        while (1) {
          //获取文件名称列表
          std::vector<std::string> list;
          data_manage.nonCompressList(&list);
          //判断是否为非热点文件
          for (int i = 0; i < list.size(); ++i) {
            bool ret = FileIsHot(list[i]);
            if (ret == false) {//如果不是热点文件就进行压缩
              std::cout << "nonhot file " << list[i];
              std::string single_src = list[i];//纯源文件
              std::string single_dst = list[i] + GZ_SUF;//纯压缩包名称
              std::string src = _backup_dir + single_src;//源文件路径名
              std::string dst = _gz_dir + single_dst;
              if (CompressUtil::Compress(src, dst) == true) {//压缩成功之后再更新数据信息
                data_manage.insert(single_src, single_dst);
                unlink(src.c_str());//更改保存了新文件的名称之后删除原文件名称
              }
            }
          }
          sleep(INTREVAL_TIME); 
        }
        //检测等待时间
        return true;
      }
    private:
      bool FileIsHot(const std::string &name){//判断文件是否为热点文件
        time_t cur_time = time(NULL);//获取当前的访问时间
        struct stat st;
        if (stat(name.c_str(), &st) < 0) {
          std::cout << name << " get stat failed!\n";
          return false;
        }
        if ((cur_time - st.st_atime) > NONHOT_TIME) {
          return false;
        }
        return true;
      }
    private:
      std::string _backup_dir;//压缩前文件的存储路径
      std::string _gz_dir;//压缩文件的存放路径
  };
  class Server {
    public:
      Server() {}
      ~Server() {}
      bool Start() {//启动网络通信模块接口
        _server.Put("/(.*)", Upload);
        _server.Get("/list", List);
        _server.Get("/download/(.*)", Download);//捕捉任意字符串

        _server.listen("0.0.0.0", 22);//开始监听
        return true;
      }
    private:
      static void Upload(const httplib::Request &req, httplib::Response &res){//上传处理回调函数
        std::string filename = req.matches[1];//捕捉到的文件名
        std::string pathname = BACKUP_DIR + filename;//文件备份路径
        FileUtil::Write(pathname, req.body);//向文件写入数据
        data_manage.insert(filename, filename);//将文件信息添加到信息管理模块

        res.status = 200;
        return;
      }

      static void List(const httplib::Request &req, httplib::Response &res) {//文件列表处理回调函数
        std::vector<std::string> list;
        data_manage.getAllName(&list);
        std::stringstream tmp;
        tmp << "<html><body><hr />";//头部标签
        for (int i = 0; i < list.size(); ++i) {
          tmp << "<a href='/download/" << list[i] << "'>" << list[i] << "</a>";
          tmp << "<hr />";
        }
        tmp << "<hr /><body><html>";//结尾

        res.set_content(tmp.str().c_str(), tmp.str().size(), "text/html");
        res.status = 200;
        return;
      }

      static void Download (const httplib::Request &req, httplib::Response &res){//文件下载处理回调函数
        std::string filename = req.matches[1];
        if (data_manage.exists(filename) == false) {
          res.status = 404;//文件不存在
          return;
        }
        std::string pathname = BACKUP_DIR + filename;//原文将备份路径
        if (data_manage.isCompress(filename) == true) {//文件已经压缩则进行解压处理
          std::string gzfile;
          data_manage.getGzName(filename, &gzfile);
          std::string gzfilepath = GZFILE_DIR + gzfile;//压缩包路径名
          CompressUtil::UnCompress(gzfilepath, pathname);//将压缩包解压
          unlink(gzfilepath.c_str());//解压之后删除压缩包
          data_manage.insert(filename, filename);//更新数据信息
        }
        //从文件中读取数据响应给客户端
        FileUtil::Read(pathname, &res.body);//文件没有压缩 将文件读取到res的body中
        res.set_content("Content-type", "application/octet-stream");//二进制流下载
        res.status = 200;
        return;
      }
    private:
      std::string _file_dir;//文件上传备份路径
      httplib::Server _server;
  };
};
