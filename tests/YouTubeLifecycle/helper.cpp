#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <iostream>
#include <string>
int main()
{
    std::string line;
    std::getline(std::cin, line);
    auto input = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
    auto mode = input.value("mode").toString();
    if (mode == "empty") return 0;
    if (mode == "invalid") { std::cout << "invalid\n" << std::flush; return 0; }
    if (mode == "error") {
        std::cout << "{\"error\":\"deadlineExceeded\",\"grpcStatus\":4}\n" << std::flush;
        return 0;
    }
    if (input.value("pageToken").toString() != "resume") return 2;
    std::cout << "{\"nextPageToken\":\"one\",\"items\":[]}\n" << std::flush;
    QThread::msleep(100);
    std::cout << "{\"nextPageToken\":\"two\",\"items\":[]}\n" << std::flush;
    if (mode == "offline") std::cout << "{\"offlineAt\":\"2026-09-29T17:00:00Z\"}\n" << std::flush;
    if (mode == "quiet") QThread::sleep(30);
    return 0;
}
