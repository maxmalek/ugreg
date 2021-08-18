#pragma once

class WebServer;

class RequestHandler
{
protected:
    RequestHandler(WebServer& sv, const char *endpoint, );
    ~RequestHandler();

private:
    WebServer& _srv;
};
