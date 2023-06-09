#include <regex>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <chrono>
#include <errno.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>
#include <google/protobuf/util/time_util.h>

#include "dfslib-shared-p1.h"
#include "dfslib-clientnode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

using dfs_service::File;
using dfs_service::FileStatus;
using dfs_service::Files;
using dfs_service::FileStream;
using dfs_service::ServerResponse;
using dfs_service::VoidArg;

using google::protobuf::RepeatedPtrField;
using google::protobuf::util::TimeUtil;

using std::chrono::system_clock;
using std::chrono::milliseconds;
using namespace std;

//
// STUDENT INSTRUCTION:
//
// You may want to add aliases to your namespaced service methods here.
// All of the methods will be under the `dfs_service` namespace.
//
// For example, if you have a method named MyMethod, add
// the following:
//
//      using dfs_service::MyMethod
//


DFSClientNodeP1::DFSClientNodeP1() : DFSClientNode() {}

DFSClientNodeP1::~DFSClientNodeP1() noexcept {}

StatusCode DFSClientNodeP1::Store(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept and store a file.
    //
    // When working with files in gRPC you'll need to stream
    // the file contents, so consider the use of gRPC's ClientWriter.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::CANCELLED otherwise
    //
    //


    ClientContext client_context;
    client_context.AddMetadata("file_name", filename);
    client_context.set_deadline(system_clock::now() + milliseconds(deadline_timeout));

    const string& file_path = WrapPath(filename);
    struct stat file_info;

    ifstream ifs(file_path);
    FileStream file_chunk;

    stat(file_path.c_str(), &file_info);
    int file_len = file_info.st_size;

    ServerResponse response;
    unique_ptr<ClientWriter<FileStream>> server_response = service_stub->StoreFile(&client_context, &response);

    int bytes_sent = 0;

    while(bytes_sent < file_len){
        char buff[10240];

        int bytes_remaining = file_len - bytes_sent;
        if(bytes_remaining > 10240){
            bytes_remaining = 10240;
        }

        ifs.read(buff, bytes_remaining);

        //pass buff content to file_chunk
        file_chunk.set_contents(buff, bytes_remaining);

        //write file_chunk to the stream
        server_response->Write(file_chunk);
        bytes_sent = bytes_sent + bytes_remaining;

    }

    ifs.close();
    server_response->WritesDone();
    Status store_status = server_response->Finish();

    if(!store_status.ok() && store_status.error_code() == StatusCode::INTERNAL){
        return StatusCode::CANCELLED;
    }

    return store_status.error_code();
}


StatusCode DFSClientNodeP1::Fetch(const std::string &filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept a file request and return the contents
    // of a file from the service.
    //
    // As with the store function, you'll need to stream the
    // contents, so consider the use of gRPC's ClientReader.
    //
    // The StatusCode server_response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext client_context;
    client_context.set_deadline(system_clock::now() + milliseconds(deadline_timeout));

    File request;
    request.set_file_name(filename);

    const string& file_path = WrapPath(filename);
    unique_ptr<ClientReader<FileStream>> server_response = service_stub->FetchFile(&client_context, request);

    ofstream ofs;
    FileStream file_chunk;

    //start writing file to client
    //use the server_response from calling FetchFile
    while (server_response->Read(&file_chunk)) {
        if (!ofs.is_open()){

            //if the file not exist, first open such file
            ofs.open(file_path, ios::trunc);
        }

        ofs << file_chunk.contents();

    }
    ofs.close();

    Status fetch_status = server_response->Finish();

    if(!fetch_status.ok() && fetch_status.error_code() == StatusCode::INTERNAL){
        return StatusCode::CANCELLED;
    }

    return fetch_status.error_code();
}

StatusCode DFSClientNodeP1::Delete(const std::string& filename) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // You will also need to add a request for a write lock before attempting to delete.
    //
    // If the write lock request fails, you should return a delete_status of RESOURCE_EXHAUSTED
    // and cancel the current operation.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::RESOURCE_EXHAUSTED - if a write lock cannot be obtained
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext client_context;
    client_context.set_deadline(system_clock::now() + milliseconds(deadline_timeout));

    File request;
    request.set_file_name(filename);

    ServerResponse response;

    Status delete_status = service_stub->DeleteFile(&client_context, request, &response);

    return delete_status.error_code();

}

StatusCode DFSClientNodeP1::List(std::map<std::string,int>* file_map, bool display) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list all files here. This method
    // should connect to your service's list method and return
    // a list of files using the message type you created.
    //
    // The file_map parameter is a simple map of files. You should fill
    // the file_map with the list of files you receive with keys as the
    // file name and values as the modified time (mtime) of the file
    // received from the server.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::CANCELLED otherwise
    //
    //
    ClientContext context;
    context.set_deadline(system_clock::now() + milliseconds(deadline_timeout));

    VoidArg request;
    Files response;

    Status list_status = service_stub->ListFiles(&context, request, &response);

    int time;

    for (const ServerResponse& file : response.file()) {
        time = TimeUtil::TimestampToSeconds(file.modified());
        file_map->insert(pair<string,int>(file.file_name(), time));
    }
    return list_status.error_code();
}

StatusCode DFSClientNodeP1::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. This method should
    // retrieve the status of a file on the server. Note that you won't be
    // tested on this method, but you will most likely find that you need
    // a way to get the status of a file in order to synchronize later.
    //
    // The status might include data such as name, size, mtime, crc, etc.
    //
    // The file_status is left as a void* so that you can use it to pass
    // a message type that you defined. For instance, may want to use that message
    // type after calling Stat from another method.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //

    ClientContext context;
    context.set_deadline(system_clock::now() + milliseconds(deadline_timeout));

    File request;
    request.set_file_name(filename);
    FileStatus response;

    //call server method as if call locally
    Status status = service_stub->GetStatus(&context, request, &response);

    file_status = &response;

    return status.error_code();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional code here, including
// implementations of your client methods
//



