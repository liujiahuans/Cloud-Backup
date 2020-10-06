#include "cloud_backup.hpp"
#include<pthread.h>
#include<iostream>

void compress_test(char *argv[]) {
  _cloud_sys::CompressUtil::Compress(argv[1], argv[2]);
  std::string file = argv[2];
  file += ".txt";
  _cloud_sys::CompressUtil::UnCompress(argv[2], file.c_str());
}

void data_test() {
  _cloud_sys::DataManager data_manage("./test");
  data_manage.initLoad();
  data_manage.insert("c.txt", "c.txt.gz");
  std::vector<std::string> list;
  data_manage.getAllName(&list);
  for (auto & i : list) {
    printf("%s\n", i.c_str());
  }

  printf("------------------------\n");
  list.clear();
  data_manage.nonCompressList(&list);
  for (auto & i : list) {
    printf("%s\n", i.c_str());
  }
}

void m_non_compress() {
  _cloud_sys::NonHotCompress noncom(GZFILE_DIR, BACKUP_DIR);
  noncom.Start();
  return ;
}

void thr_http_server() {
  _cloud_sys::Server serv;
  serv.Start();
  return ;
}

int main(int argc, char *argv[]) {
  if (boost::filesystem::exists(BACKUP_DIR) == false) {//备份文件路径不存在
    boost::filesystem::create_directory(BACKUP_DIR);//创建一个备份文件路径
  }
  if (boost::filesystem::exists(GZFILE_DIR) == false) {//压缩包文件路径不存在
    boost::filesystem::create_directory(GZFILE_DIR);//创建一个压缩包存放路径
  }
  
  std::thread thr_compress(m_non_compress);//c++11中的线程 启动非热点压缩模块线程
  std::thread thr_server(thr_http_server);//启动网络通信服务模块
  thr_compress.join();
  thr_server.join();//等待线程退出

  return 0;
}
