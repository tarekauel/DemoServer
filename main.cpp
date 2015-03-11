#include "server_http.hpp"

#include "AnalysisGraph/vertex.h"
#include "AnalysisGraph/relationship.h"
#include "AnalysisGraph/graph.h"
#include "AnalysisGraph/spreading_activation.h"
#include "AnalysisGraph/shortest_path.h"
#include "AnalysisGraph/impuls.h"
#include "AnalysisGraph/distance_algorithm.h"

//Added for the json-example
#define BOOST_SPIRIT_THREADSAFE
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string/replace.hpp>

//Added for the default_resource example
#include<fstream>

#include <execinfo.h>
#include <errno.h>
#include <cxxabi.h>
#include <AddressBook/AddressBook.h>

using namespace std;
using namespace SimpleWeb;
//Added for the json-example:
using namespace boost::property_tree;

std::string data_json;
std::string edges_type_json;
std::string vertex_type_json;

bool fileExist(const std::string & filename) {
    ifstream f(filename.c_str());
    if (f.good()) {
        f.close();
        return true;
    } else {
        f.close();
        return false;
    }  }

static inline void printStackTrace( FILE *out = stderr, unsigned int max_frames = 63 )
{
    fprintf(out, "stack trace:\n");

    // storage array for stack trace address data
    void* addrlist[max_frames+1];

    // retrieve current stack addresses
    unsigned int addrlen = backtrace( addrlist, sizeof( addrlist ) / sizeof( void* ));

    if ( addrlen == 0 )
    {
        fprintf( out, "  \n" );
        return;
    }

    // resolve addresses into strings containing "filename(function+address)",
    // Actually it will be ## program address function + offset
    // this array must be free()-ed
    char** symbollist = backtrace_symbols( addrlist, addrlen );

    size_t funcnamesize = 1024;
    char funcname[1024];

    // iterate over the returned symbol lines. skip the first, it is the
    // address of this function.
    for ( unsigned int i = 4; i < addrlen; i++ )
    {
        char* begin_name   = NULL;
        char* begin_offset = NULL;
        char* end_offset   = NULL;

        // find parentheses and +address offset surrounding the mangled name

        // OSX style stack trace
        for ( char *p = symbollist[i]; *p; ++p )
        {
            if (( *p == '_' ) && ( *(p-1) == ' ' ))
                begin_name = p-1;
            else if ( *p == '+' )
                begin_offset = p-1;
        }

        if ( begin_name && begin_offset && ( begin_name < begin_offset ))
        {
            *begin_name++ = '\0';
            *begin_offset++ = '\0';

            // mangled name is now in [begin_name, begin_offset) and caller
            // offset in [begin_offset, end_offset). now apply
            // __cxa_demangle():
            int status;
            char* ret = abi::__cxa_demangle( begin_name, &funcname[0],
                    &funcnamesize, &status );
            if ( status == 0 )
            {
                auto funcname = ret; // use possibly realloc()-ed string
                fprintf( out, "  %-30s %-40s %s\n",
                        symbollist[i], funcname, begin_offset );
            } else {
                // demangling failed. Output function name as a C function with
                // no arguments.
                fprintf( out, "  %-30s %-38s() %s\n",
                        symbollist[i], begin_name, begin_offset );
            }
        } else {
            // couldn't parse the line? print the whole line.
            fprintf(out, "  %-40s\n", symbollist[i]);
        }
    }

    free(symbollist);
}

void handler(int sig) {
    printStackTrace();
    exit(sig);
}

void start_server(oc::graph& g, oc::spreading_activation sa, oc::distance_algorithm da, std::string data_path, int port = 8080) {
    //HTTP-server at port 8080 using 4 threads
    Server<HTTP> server(port, 4);


    //Add resources using regular expression for path, a method-string, and an anonymous function
    //POST-example for the path /string, responds the posted string
    //  If C++14: use 'auto' instead of 'shared_ptr<Server<HTTPS>::Request>'
    server.resource["^/string/?$"]["POST"]=[](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        //Retrieve string from istream (*request.content)
        stringstream ss;
        request->content >> ss.rdbuf();
        string content=ss.str();

        response << "HTTP/1.1 200 OK\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
    };

    server.resource["^\\/spreading_activation\\?entity=(.*)$"]["GET"]=[&](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        string company=request->path_match[1];
        boost::replace_all(company, "%20", " ");

        std::cout << "Spreading Activation für " << company << std::endl;
        std::string result;
        sa.algorithm(g, company, 10, 8, 0.0001, "", result);

        response << "HTTP/1.1 200 OK\r\nContent-Length: " << result.length() << "\r\n\r\n" << result;
    };

    server.resource["^\\/distance_algorithm\\?entity1=(.*)\\&entity2=(.*)$"]["GET"]=[&](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        string company_a=request->path_match[1];
        string company_b=request->path_match[2];

        boost::replace_all(company_a, "%20", " ");
        boost::replace_all(company_b, "%20", " ");

        std::cout << "Distance für " << company_a << " " << company_b << std::endl;
        std::string result;

        da.algorithm(g, company_a, company_b, 4, result);

        response << "HTTP/1.1 200 OK\r\nContent-Length: " << result.length() << "\r\n\r\n" << result;
    };

    //POST-example for the path /json, responds firstName+" "+lastName from the posted json
    //Responds with an appropriate error message if the posted json is not valid, or if firstName or lastName is missing
    //Example posted json:
    //{
    //  "firstName": "John",
    //  "lastName": "Smith",
    //  "age": 25
    //}
    server.resource["^/json/?$"]["POST"]=[](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        try {
            ptree pt;
            read_json(request->content, pt);

            string name=pt.get<string>("firstName")+" "+pt.get<string>("lastName");

            response << "HTTP/1.1 200 OK\r\nContent-Length: " << name.length() << "\r\n\r\n" << name;
        }
        catch(exception& e) {
            response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
        }
    };

    server.resource["^/vertices/?$"]["POST"]=[&](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        try {
            ptree pt;
            read_json(request->content, pt);

            string id=pt.get<string>("__id");
            string name=pt.get<string>("name");
            string type=pt.get<string>("type");

            oc::vertex* v = g.get_vertex(id);
            v->add_property("type", type);
            v->add_property("name", name);

            bool exists = fileExist(data_path + "/custom_vertices.csv");

            std::ofstream f(data_path + "/custom_vertices.csv", std::ifstream::out | std::fstream::app);

            if (!exists) {
                f << "\"id\",\"name\",\"type\"";
            }

            f << std::endl << "\"" << id << "\",\"" << name << "\",\"" << type << "\"";

            response << "HTTP/1.1 201 Created\r\nContent-Length: 0" << "\r\n\r\n";

            std::cout << "Saved vertex: " << "\"" << id << "\",\"" << name << "\",\"" << type << "\"" << std::endl;
        }
        catch(exception& e) {
            response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
        }
    };

    server.resource["^/edges/?$"]["POST"]=[&](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        try {
            ptree pt;
            read_json(request->content, pt);

            string source=pt.get<string>("source");
            string target=pt.get<string>("target");
            string type=pt.get<string>("type");

            oc::vertex* s = g.get_vertex(source);
            oc::vertex* t = g.get_vertex(target);
            s->add_out(t, type);

            bool exists = fileExist(data_path + "/custom_edges.csv");

            std::ofstream f(data_path + "/custom_edges.csv", std::ifstream::out | std::fstream::app);

            if (!exists) {
                f << "\"src\",\"dst\",\"type\"";
            }

            f << std::endl << "\"" << source << "\",\"" << target << "\",\"" << type << "\"";

            response << "HTTP/1.1 201 Created\r\nContent-Length: 0" << "\r\n\r\n";

            std::cout << "Saved edge " << "\"" << source << "\",\"" << target << "\",\"" << type << "\"" << std::endl;
        }
        catch(exception& e) {
            response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << strlen(e.what()) << "\r\n\r\n" << e.what();
        }
    };

    //GET-example for the path /info
    //Responds with request-information
    server.resource["^/info/?$"]["GET"]=[](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        stringstream content_stream;
        content_stream << "<h1>Request:</h1>";
        content_stream << request->method << " " << request->path << " HTTP/" << request->http_version << "<br>";
        for(auto& header: request->header) {
            content_stream << header.first << ": " << header.second << "<br>";
        }

        //find length of content_stream (length received using content_stream.tellp())
        content_stream.seekp(0, ios::end);

        response <<  "HTTP/1.1 200 OK\r\nContent-Length: " << content_stream.tellp() << "\r\n\r\n" << content_stream.rdbuf();
    };

    //GET-example for the path /match/[number], responds with the matched string in path (number)
    //For instance a request GET /match/123 will receive: 123
    server.resource["^/data.json$"]["GET"]=[](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        std::cout << "Sending data.json" << std::endl;
        response << "HTTP/1.1 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Content-Length: " << data_json.length() << "\r\n\r\n" << data_json;
    };

    server.resource["^/edgeTypes.json$"]["GET"]=[](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        std::cout << "Sending data.json" << std::endl;
        response << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: application/json\r\n"
                << "Content-Length: " << edges_type_json.length() << "\r\n\r\n" << edges_type_json;
    };

    server.resource["^/vertexTypes.json$"]["GET"]=[](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        std::cout << "Sending data.json" << std::endl;
        response << "HTTP/1.1 200 OK\r\n"
                << "Content-Type: application/json\r\n"
                << "Content-Length: " << vertex_type_json.length() << "\r\n\r\n" << vertex_type_json;
    };

    //Default GET-example. If no other matches, this anonymous function will be called.
    //Will respond with content in the web/-directory, and its subdirectories.
    //Default file: index.html
    //Can for instance be used to retrieve an HTML 5 client that uses REST-resources on this server
    server.default_resource["^/?(.*)$"]["GET"]=[](ostream& response, shared_ptr<Server<HTTP>::Request> request) {
        string filename="public/";

        string path=request->path_match[1];

        //Replace all ".." with "." (so we can't leave the web-directory)
        size_t pos;
        while((pos=path.find(".."))!=string::npos) {
            path.erase(pos, 1);
        }

        filename+=path;
        ifstream ifs;
        //A simple platform-independent file-or-directory check do not exist, but this works in most of the cases:
        if(filename.find('.')==string::npos) {
            if(filename[filename.length()-1]!='/')
                filename+='/';
            filename+="index.html";
        }
        ifs.open(filename, ifstream::in);

        if(ifs) {
            ifs.seekg(0, ios::end);
            size_t length=ifs.tellg();

            ifs.seekg(0, ios::beg);

            //The file-content is copied to the response-stream. Should not be used for very large files.
            response << "HTTP/1.1 200 OK\r\n"
                     << "Content-Length: " << length << "\r\n\r\n" << ifs.rdbuf();

            ifs.close();
        }
        else {
            string content="Could not open file "+filename;
            response << "HTTP/1.1 400 Bad Request\r\nContent-Length: " << content.length() << "\r\n\r\n" << content;
        }
    };

    thread server_thread([&server](){
        //Start server
        server.start();
    });

    server_thread.join();
}

void init(oc::graph& g, const std::string& data_path) {

    std::vector<std::string> vertex_files {"City", "Country", "Region","Advisor", "Category", "Founder", "FundingRound", "HQ", "keywords", "Member", "Office", "organizations", "PrimaryImage", "TeamMember", "Website","companies_acquired_by_sap"};

    std::chrono::time_point<std::chrono::system_clock> start, end;
    start = std::chrono::system_clock::now();

    try {
        for (auto f : vertex_files) {
            g.add_vertices_by_file(data_path + f + ".csv", {"path","url"});
        }
    } catch (std::string str) {
        std::cerr << str << std::endl;
        throw str;
    }

    g.add_edges_by_file(data_path + "edges_dump.csv");

    if (fileExist(data_path + "custom_edges.csv")) {
        g.add_edges_by_file(data_path + "custom_edges.csv", ",");
    }

    if (fileExist(data_path + "custom_vertices.csv")) {
        g.add_vertices_by_file(data_path + "custom_vertices.csv", {"id"});
    }

    end = std::chrono::system_clock::now();
    std::cout << "Parsed all in " << std::chrono::duration_cast<std::chrono::milliseconds>(end-start).count() <<
            " ms" << std::endl;

    std::cout << std::endl << g << std::endl;

    std::vector<std::string> types {"Organization","City","Category","keyword","Region","Country","Person","TeamMember","Founder","Advisor"};
    /*std::ofstream auto_file {output_path + "auto.csv"};
    auto_file << "__id,type,name";
    for (auto v : g.get_vertices()) {
        if (std::find(types.begin(),types.end(),v->get_property("type")) != types.end()) {
            auto_file << std::endl << "\"" << v->get_identifier() << "\",\"" << v->get_property("type") << "\",\"" << v->get_alias() << "\"";
        }
    }*/
}

std::string build_auto_suggest(oc::graph& g) {
    std::vector<oc::vertex*> vertices = g.get_vertices();
    std::string result = "[";
    bool first = true;
    std::vector<std::string> allowed_type {"Organization","City","Category","keyword","Region","Country","Person","TeamMember","Founder","Advisor"};
    int i =0;
    for (auto v : vertices) {
        ++i;
        std::string type = v->get_property("type");
        std::string name = v->get_alias();
        std::string id = v->get_identifier();
        if (type != "" && name != "") {
            if (std::find(allowed_type.begin(),allowed_type.end(),type) != allowed_type.end() && v->get_alias() != v->get_identifier()) {
                if (first) first = false;
                else result += ",";
                result += "{\"__id\":\"" + id + "\",";
                result += "\"type\":\"" + type + "\",";
                result += "\"name\":\"" + name + "\"}\r\n";
            }
        }
    }
    boost::replace_all(result, "\\'", "'");
    boost::replace_all(result, "\t", " ");
    return result + "]";
}

std::string build_auto_suggest_edges(oc::graph& g) {
    std::vector<oc::vertex*> vertices = g.get_vertices();
    std::vector<std::string> types;
    for (auto v : vertices) {
        vector<oc::relationship> rel_out = v->get_rel_out();
        for (auto r : rel_out) {
            if (r.get_type() != "") types.push_back(r.get_type());
        }
    }

    std::sort(types.begin(),types.end());
    types.erase(std::unique(types.begin(),types.end()),types.end());
    std::string result = "[";
    bool first = true;
    for (auto s : types) {
        if (first) first = false;
        else result += ",";
        result += "\"" + s + "\"";
    }
    return result + "]";
}

std::string build_auto_suggest_vertex(oc::graph& g) {
    std::vector<oc::vertex*> vertices = g.get_vertices();
    std::vector<std::string> types;
    for (auto v : vertices) {
        if (v->get_property("type") != "") {
            types.push_back(v->get_property("type"));
        }
    }

    std::sort(types.begin(),types.end());
    types.erase(std::unique(types.begin(),types.end()),types.end());
    std::string result = "[";
    bool first = true;
    for (auto s : types) {
        if (first) first = false;
        else result += ",";
        result += "\"" + s + "\"";
    }
    return result + "]";
}


int main(int argc, char* argv[]) {
    signal(SIGSEGV, handler);

    std::string data_path = "";
    int port = 8080;

    if (argc >= 2) {
        data_path = argv[1];
        if (argc >= 3) {
            port = std::stoi(argv[2]);
        }
    } else {
        std::cerr << "Please provide the data path as first argument" << std::endl;
        return 1;
    }

    std::cout << "Data path: " << data_path << std::endl;
    std::cout << "Port: " << port << std::endl;

    oc::graph g{};

    oc::spreading_activation spreading_activation;
    oc::distance_algorithm distance_algorithm;

    init(g,data_path);

    data_json = build_auto_suggest(g);
    edges_type_json = build_auto_suggest_edges(g);
    vertex_type_json = build_auto_suggest_vertex(g);

    start_server(g, spreading_activation, distance_algorithm, data_path, port);

    return 0;
}