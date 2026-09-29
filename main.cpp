#include <iostream>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string>
#include <map>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <fstream>
#include <sstream>
using namespace std;

string readFile(const string& path){
    ifstream file(path);
    if(!file.is_open()){
        return "";
    }
    stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

string generateCode(){ //tao short code
    string chars = "qwertyuiopasdfghjklzxcvbnm123456789";
    string code = "";
    for(int i = 0;i < 6;i++){
        code += chars[rand() % chars.size()];
    }
    return code;
}


bool isValidUrl(const string& url){
    if(url.size() < 8 || url.size() > 2048) return false;
    if(url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return false;
    for(unsigned char c : url){
        // ky tu dieu khien, khoang trang, dau " va \ deu bi cam
        // (tranh hong JSON va tranh chen header vao response)
        if(c <= 0x20 || c == 0x7f || c == '"' || c == '\\') return false;
    }
    return true;
}

// THEM: lay Content-Length tu header
size_t getContentLength(const string& headers){
    string lower = headers;
    for(auto& c : lower) c = tolower(c);
    size_t pos = lower.find("content-length:");
    if(pos == string::npos) return 0;
    return stoul(lower.substr(pos + 15));
}

/*1. Status Line HTTP/1.1 200 OK
2. Header Content-Type: text/html; charset=utf-8 Content-Length: 42 Connection: close
3.Body <h1>Xin chào từ dududu</h1>*/
void sendResponse(int client, int status,const string& statusText,const string& contentType,const string& body,const string extraHeader =""){
    string response = "";

    response += "HTTP/1.1 " + to_string(status) +" " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";      
    response += "Content-Length: " + to_string(body.size()) + "\r\n"; 
    response += "Connection: close\r\n";
    response += extraHeader;
    response += "\r\n";                                       
    response += body;

    write(client,response.c_str(),response.size());
}

int main() {
    srand(time(nullptr));
    signal(SIGPIPE, SIG_IGN); 

    int server_fd = socket(AF_INET, SOCK_STREAM, 0); //tao socket

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); 

    map<string,string> mp;
    mp.insert({"/abc123","https://www.google.com/"});
    mp.insert({"/xyz456","https://www.facebook.com/"});
    mp.insert({"/mlq789","https://www.youtube.com/"});

    sockaddr_in addr{}; // tao dia chi
    addr.sin_port = htons(8080); //tao port:8080
    addr.sin_family = AF_INET; //tao IPv4
    addr.sin_addr.s_addr = INADDR_ANY;

    
    if(bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0){ //gan dia chi vao socket
        cerr << "Bind loi" << endl;
        return 1;
    }
    if(listen(server_fd,10) < 0){ // socket san sang nghe request
        cerr << "Listen loi" << endl;
        return 1;
    }

    cout << "Server chay tai http://localhost:8080" << endl;

    while(true){
        int client_fd = accept(server_fd,nullptr,nullptr);

        // THEM: timeout 5s, tranh bi treo boi ket noi rong cua trinh duyet
        timeval tv{5, 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        char buf[4096] ={}; //tao o chua request

        cout << "Waiting read..." << endl;
        ssize_t n = read(client_fd,buf,sizeof(buf) - 1); // SUA: chua 1 cho cho ky tu ket thuc
        if(n <= 0){ // ket noi rong hoac timeout
            close(client_fd);
            continue;
        }

        string request(buf, n); //chuyen requets tu char sang string (co do dai n)

        // THEM: tim het header, roi doc tiep cho du body theo Content-Length
        size_t headerEnd = request.find("\r\n\r\n");
        if(headerEnd == string::npos){
            sendResponse(client_fd,400,"Bad Request","application/json",R"({"error":"Request ko hop le"})");
            close(client_fd);
            continue;
        }

        size_t contentLength = 0;
        try{
            contentLength = getContentLength(request.substr(0, headerEnd));
        }
        catch(...){
            contentLength = 0;
        }
        if(contentLength > 4096){ // gioi han body
            sendResponse(client_fd,413,"Payload Too Large","application/json",R"({"error":"Body qua lon"})");
            close(client_fd);
            continue;
        }

        while(request.size() < headerEnd + 4 + contentLength){ // body chua den du thi doc tiep
            ssize_t m = read(client_fd,buf,sizeof(buf) - 1);
            if(m <= 0) break;
            request.append(buf, m);
        }

        cout << "have read" << endl;

        size_t firstSpace = request.find(" ");
        size_t secondSpace = request.find(' ',firstSpace+1);
        string first_line = request.substr(0,request.find("\r\n")); //dong dau tien trong requets Get /path HTTP/1.1
        cout << "Dong dau: " << first_line << endl;

        if(firstSpace == string::npos || secondSpace == string::npos){
            sendResponse(client_fd,400,"Bad Request","application/json",R"({"error":"Request ko hop le"})");
            close(client_fd);   // SUA: truoc day thieu close + continue nen code chay tiep va co the crash
            continue;
        }

        

        string method = first_line.substr(0, first_line.find(" ")); //lay Method
        string path = first_line.substr(method.size() + 1, first_line.find(" HTTP") - method.size() - 1);
        cout << "Method: " << method << " | Path: " << path << endl;

        if(method == "GET" && path == "/"){ // path trong
            string html = readFile("index.html");
            if(html.empty()){
                sendResponse(client_fd,404,"Not Found","text/plain","index.html not found");
            }
            else{
                sendResponse(client_fd,200,"OK","text/html; charset=UTF-8",html);
            }
            close(client_fd);
            continue;
        }
        else if(method == "GET" && path == "/style.css"){ // THEM: route de browser tai duoc CSS
            string css = readFile("style.css");
            if(css.empty()){
                sendResponse(client_fd,404,"Not Found","text/plain","style.css not found");
            }
            else{
                sendResponse(client_fd,200,"OK","text/css; charset=UTF-8",css);
            }
            close(client_fd);
            continue;
        }
        else if(method == "GET" && path == "/hello"){
            sendResponse(client_fd,200,"OK","text/plain; charset=UTF-8","Xin chao!");
            close(client_fd);
            continue;
        }
        else if(method == "POST" && path == "/shorten"){ 
            string body_str = "";

            body_str = request.substr(headerEnd + 4); //phan body nam sau "\r\n\r\n"

            if(body_str.empty()){
                sendResponse(client_fd,400,"Bad Request","application/json",R"({"error":"Chua nhap URL"})");
            }
            else if(!isValidUrl(body_str)){ // THEM: validate URL
                sendResponse(client_fd,400,"Bad Request","application/json",R"({"error":"URL ko hop le"})");
            }
            else{
                string code;
                do{
                    code = generateCode();
                } while(mp.count("/" + code));  //lap lai neu code bi trung

                mp["/" + code] = body_str;

                
                string json = "{\"shortUrl\":\"http://localhost:8080/" + code + "\"}";
                sendResponse(client_fd,200,"OK","application/json",json);
            }
            close(client_fd);
            continue;
        }
        else if(method == "GET" && mp.count(path)){ // redirect
            string long_URL = mp[path];
            
            sendResponse(client_fd,302,"Found","text/plain","","Location: " + long_URL + "\r\n");
            close(client_fd);
            continue;
        }
        else{
            sendResponse(client_fd,404,"Not Found","application/json",R"({"error":"Khong tim thay resource"})");
            close(client_fd);
            continue;
        }
    }

    close(server_fd);
    return 0;
}