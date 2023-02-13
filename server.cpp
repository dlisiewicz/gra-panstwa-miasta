#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <error.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <thread>
#include <unordered_set>
#include <signal.h>
#include <cstring>
#include <chrono>
#include <cmath>
#include <vector>
#include <fstream>
#include <map>

class Client;

int servFd;
int epollFd;
std::unordered_set<Client*> clients;

bool gameStarted = false;
int activeInGame = 0;
int gameSeconds = 0;
char letter;


void ctrl_c(int);

void sendToAllBut(int fd, char * buffer, int count);
void sendToAll(char * buffer, int count);
void sendToOne(int fd, char * buffer, int count);
void sendToAllActive(char * buffer, int count);
char* removeFirst(const char* buffer);
bool checkNickname(int fd, char * buffer);
void checkAnswers();
int countActive();
void startGame();
void sendList();
bool checkIfStop();
void gameTimer();
void fBuf();

uint16_t readPort(char * txt);
void setReuseAddr(int sock);

std::vector<std::string> readFromFile(std::string fileName) {
    std::vector<std::string> strings;
    std::ifstream file(fileName);
    if (!file.is_open()) {
        std::cerr << "Nie mozna otworzyc pliku " << fileName << std::endl;
        return strings;
    }
    std::string line;
    while (file >> line) {
        strings.push_back(line);
    }
    file.close();
    return strings;
}

std::vector<std::string> panstwa;
std::vector<std::string> miasta;
std::vector<std::string> imiona;

struct Handler {
    virtual ~Handler(){}
    virtual void handleEvent(uint32_t events) = 0;
};

class Client : public Handler {
    int _fd;

public:
    char* nickname;
    char* answer;
    float points;
    bool active;
    bool answered;
    std::string fb;
    Client(int fd) : _fd(fd) {
        epoll_event ee {EPOLLIN|EPOLLRDHUP, {.ptr=this}};
        epoll_ctl(epollFd, EPOLL_CTL_ADD, _fd, &ee);
        points = 0.0;
        active = false;
        answered = false;
        answer = nullptr;
        nickname = nullptr;
        fb ="";
    }
    virtual ~Client(){
        epoll_ctl(epollFd, EPOLL_CTL_DEL, _fd, nullptr);
        shutdown(_fd, SHUT_RDWR);
        close(_fd);
    }
    int fd() const {return _fd;}
    virtual void handleEvent(uint32_t events) override {
        if(events & EPOLLIN) {
            char buffer[256];
            bzero(buffer, 256);
            ssize_t count = read(_fd, buffer, 256);
            if(count > 0){
                this->fb+=buffer;
            }

            else
                events |= EPOLLERR;
        }
        if(events & ~EPOLLIN){
            remove();
        }
    }
    void write(char * buffer, int count){
        if(count != ::write(_fd, buffer, count)){
            remove();
        }
    }
    void remove() {
        printf("removing %d\n", _fd);
        clients.erase(this);
        delete this;

        if(countActive()<2){
            gameStarted = false;
        }
        sendList();
    }

};

class : Handler {
    public:
    virtual void handleEvent(uint32_t events) override {
        if(events & EPOLLIN){
            sockaddr_in clientAddr{};
            socklen_t clientAddrSize = sizeof(clientAddr);
            
            auto clientFd = accept(servFd, (sockaddr*) &clientAddr, &clientAddrSize);
            if(clientFd == -1) error(1, errno, "accept failed");
            
            printf("new connection from: %s:%hu (fd: %d)\n", inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port), clientFd);
            
            clients.insert(new Client(clientFd));
        }
        if(events & ~EPOLLIN){
            error(0, errno, "Event %x on server socket", events);
            ctrl_c(SIGINT);
        }
    }
} servHandler;

int main(int argc, char ** argv){
    if(argc != 2) error(1, 0, "Need 1 arg (port)");
    auto port = readPort(argv[1]);

    panstwa = readFromFile("odpowiedzi/panstwa.txt");
    miasta = readFromFile("odpowiedzi/miasta.txt");
    imiona = readFromFile("odpowiedzi/imiona.txt");

    servFd = socket(AF_INET, SOCK_STREAM, 0);
    if(servFd == -1) error(1, errno, "socket failed");
    
    signal(SIGINT, ctrl_c);
    signal(SIGPIPE, SIG_IGN);
    
    setReuseAddr(servFd);
    
    sockaddr_in serverAddr{.sin_family=AF_INET, .sin_port=htons((short)port), .sin_addr={INADDR_ANY}};
    int res = bind(servFd, (sockaddr*) &serverAddr, sizeof(serverAddr));
    if(res) error(1, errno, "bind failed");
    
    res = listen(servFd, 1);
    if(res) error(1, errno, "listen failed");


    epollFd = epoll_create1(0);
    
    epoll_event ee {EPOLLIN, {.ptr=&servHandler}};
    epoll_ctl(epollFd, EPOLL_CTL_ADD, servFd, &ee);
    
    std::thread t(gameTimer);
    std::thread t1(fBuf);

    while(true){
        if(-1 == epoll_wait(epollFd, &ee, 1, -1)) {
            error(0,errno,"epoll_wait failed");
            ctrl_c(SIGINT);
        }
        ((Handler*)ee.data.ptr)->handleEvent(ee.events);
    }
}
void fBuf(){
    char buf[256];
    char msg[256];
    bzero(buf, 256);
    bzero(msg, 256);

    while(1){
        auto it = clients.begin();
        while(it!=clients.end()){
            Client * client = *it;
            it++;
            if(client->fb.size()>0){
                for(int i=0; i<int(client->fb.size()); i++){
                    if(client->fb[i]!='\n'){
                        sprintf(buf, "%c", client->fb[i]);
                        strcat(msg, buf);
                    }
                    else if(client->fb[i]=='\n'){
                        if(msg[0]=='N'){
                            if(checkNickname(client->fd(), removeFirst(msg))){
                                client->nickname=removeFirst(msg);
                                strcpy(buf, "valid\n");
                                sendToOne(client->fd(), buf, strlen(buf));
                                client->active = true;
                                sendList();

                                if(!gameStarted){
                                    if(countActive()>1){
                                        startGame();
                                    }
                                }
                            }
                            else{
                                strcpy(buf, "invalid\n");
                                sendToOne(client->fd(), buf, strlen(buf));
                            }
                        }
                        if(msg[0]=='A'){
                            client->answer=removeFirst(msg);
                            client->answered=true;
                            sendList();
                            if(checkIfStop()){
                                checkAnswers();
                                startGame();
                                sendList();
                            }
                        }
                        client->fb.erase(0, i+1);
                        bzero(msg, 256);
                    }

                }
            }
            bzero(msg, 256);
        }
    }
}
void startGame(){
    char msg[256];
    char buf[256];
    bzero(buf, 256);
    bzero(msg, 256);

    auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        client->answered=false;
        client->answer=nullptr;
    }


    srand(time(0));
    strcpy(msg, "start");
    letter = char(rand() % 26 + 65);
    sprintf(buf, "%c", letter);
    strcat(msg, buf);
    strcat(msg, "\n");

    sendToAllActive(msg, strlen(msg));
    gameStarted = true;
    gameSeconds = 0;


}
void gameTimer() {
    while(1){
        if(gameStarted){
            std::this_thread::sleep_until(std::chrono::system_clock::now() + std::chrono::seconds(1));
            gameSeconds++;
            if(gameSeconds==45){
                checkAnswers();
                startGame();
                sendList();
           }
        }
    }
}
void sendList(){ 
    char msg[256];
    char buf[256];
    bzero(buf, 256);
    bzero(msg, 256);

    auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        
        if(client->active==true){
            strcpy(buf, client->nickname);
            strcat(msg, buf);
            sprintf(buf, " %i ", int(client->points));
            strcat(msg, buf);
            if(client->answered){
                strcat(msg, "Tak;");
            }
            else if(!client->answered){
                strcat(msg, "Nie;");
            }   
        }
    }
    strcat(msg, "\n");
    sendToAllActive(msg, strlen(msg));


}
bool checkNickname(int fd, char * buffer){
    auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        if(client->fd()!=fd && client->nickname!=0){
            if(strcmp(client->nickname, buffer)==0){
            return false;
            }
        }
    }
    return true;
}
void checkAnswers(){
    std::map<std::string, int> answerCountCountry;
    std::map<std::string, int> answerCountCity;
    std::map<std::string, int> answerCountName;

    std::vector<std::string> answers;
    std::string buf = "";
    std::string answer;
    
    auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        if(client->answer==0){
            client->points+=0;
        }
        else if(client->answer!=0){
            answer = client->answer;

            int answerSize = answer.length();
            for(int i=0; i<answerSize; i++){
                if(client->answer[i]!=';'){
                    buf+=client->answer[i];

                }
                else if(client->answer[i]==';'){
                    answers.push_back(buf);
                    buf="";
                }
            }
            answers.push_back(buf);
            buf="";

            if(answers[0][0]==letter){
                for (std::string a : panstwa) {
                    if(answers[0] == a){
                        if (answerCountCountry.count(a) == 0) {
                            answerCountCountry[a] = 1;
                        } 
                        else {
                            answerCountCountry[a]++;
                        }
                    }
                }
            }
            if(answers[1][0]==letter){
                for (std::string &a : miasta) {
                    if(answers[1] == a){
                        if (answerCountCity.count(a) == 0) {
                            answerCountCity[a] = 1;
                        } 
                        else {
                            answerCountCity[a]++;
                        }
                    }
                }
            }
            if(answers[2][0]==letter){
                for (const std::string &a : imiona) {
                    if(answers[2] == a){
                        if (answerCountName.count(a) == 0) {
                            answerCountName[a] = 1;
                        } 
                        else {
                            answerCountName[a]++;
                        }
                    }
                }
            }
            answers.clear();
            
        }
    }

    auto it2 = clients.begin();
    while(it2!=clients.end()){
        Client * client = *it2;
        it2++;
        if(client->answer!=0){
            answer = client->answer;
            int answerSize = answer.length();
            for(int i=0; i<answerSize; i++){
                if(client->answer[i]!=';'){
                    buf+=client->answer[i];

                }
                else if(client->answer[i]==';'){
                    answers.push_back(buf);
                    buf="";
                }
            }
            answers.push_back(buf);
            buf="";

            if(answerCountCountry.find(answers[0])!=answerCountCountry.end()) {
                if(answerCountCountry.size()==1 && answerCountCountry[answers[0]]==1) client->points+=15;
                if(answerCountCountry.size()>1 && answerCountCountry[answers[0]]==1) client->points+=10;
                if(answerCountCountry[answers[0]]>1) client->points+=5;
            }
            if(answerCountCity.find(answers[1])!=answerCountCity.end()) {
                if(answerCountCity.size()==1 && answerCountCity[answers[1]]==1) client->points+=15;
                if(answerCountCity.size()>1 && answerCountCity[answers[1]]==1) client->points+=10;
                if(answerCountCity[answers[1]]>1) client->points+=5;
            }
            if(answerCountName.find(answers[2])!=answerCountName.end()) {
                if(answerCountName.size()==1 && answerCountName[answers[2]]==1) client->points+=15;
                if(answerCountName.size()>1 && answerCountName[answers[2]]==1) client->points+=10;
                if(answerCountName[answers[2]]>1) client->points+=5;
            }

            answers.clear();
        }
    }

}

bool checkIfStop(){
auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        if(client->answered==false){
            return false;
        }
    }
    return true;
}
uint16_t readPort(char * txt){
    char * ptr;
    auto port = strtol(txt, &ptr, 10);
    if(*ptr!=0 || port<1 || (port>((1<<16)-1))) error(1,0,"illegal argument %s", txt);
    return port;
}

void setReuseAddr(int sock){
    const int one = 1;
    int res = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if(res) error(1,errno, "setsockopt failed");
}

void ctrl_c(int){
    for(Client * client : clients)
        delete client;
    close(servFd);
    printf("Closing server\n");
    exit(0);
}

void sendToAllBut(int fd, char * buffer, int count){
    auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        if(client->fd()!=fd){
            client->write(buffer, count);
        }
    }
}
void sendToAll(char * buffer, int count){
    auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        client->write(buffer, count);    
    }
}
void sendToOne(int fd, char * buffer, int count){
    auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        if(client->fd()==fd){
            client->write(buffer, count);
        }
    }
}
void sendToAllActive(char * buffer, int count){
    auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        if(client->active==true){
            client->write(buffer, count);    
        }
    }
}
char* removeFirst(const char* buffer) {
    int len = strlen(buffer);
    char* buffer2 = new char[len - 1];

    int i, j = 0;
    for (i = 0; i < len; i++) {
        if (i == 0) continue;
        buffer2[j++] = buffer[i];
    }
    buffer2[j] = '\0';
    return buffer2;
}
int countActive(){
    int active = 0;
    auto it = clients.begin();
    while(it!=clients.end()){
        Client * client = *it;
        it++;
        if(client->active==true){
            active++;
        }     
    }
    return active;
}