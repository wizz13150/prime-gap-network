#include <QCoreApplication>
#include <QJsonDocument>
#include <QVariant>
#include <QJsonObject>
#include <iostream>
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);
    for(const auto literal:{"0","9007199254740993","9223372036854775807","9223372036854775808","1.0","1e3","9007199254740993.0"}){
        const auto value=QJsonDocument::fromJson(QByteArray("{\"n\":")+literal+"}").object().value("n");
        std::cout<<literal<<" type="<<value.toVariant().metaType().name()<<" toInteger="<<value.toInteger(-1)<<'\n';
    }
}
