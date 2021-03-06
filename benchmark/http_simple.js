libDir = node.path.join(node.path.dirname(__filename), "../lib");
node.libraryPaths.unshift(libDir);

node.mixin(require("/utils.js"));
http = require("/http.js");

fixed = ""
for (var i = 0; i < 20*1024; i++) {
  fixed += "C";
}
stored = {};
http.createServer(function (req, res) {
  var commands = req.uri.path.split("/");
  var command = commands[1];
  var body = "";
  var arg = commands[2];
  var status = 200;

  p(req.uri.params);

  if (command == "bytes") {
    var n = parseInt(arg, 10)
    if (n <= 0)
      throw "bytes called with n <= 0" 
    if (stored[n] === undefined) {
      puts("create stored[n]");
      stored[n] = "";
      for (var i = 0; i < n; i++) {
        stored[n] += "C"
      }
    }
    body = stored[n];

  } else if (command == "quit") {
    res.connection.server.close();
    body = "quitting";

  } else if (command == "fixed") {
    body = fixed;

  } else {
    status = 404;
    body = "not found\n";
  }

  var content_length = body.length.toString();

  res.sendHeader( status 
                , { "Content-Type": "text/plain"
                  , "Content-Length": content_length
                  }
                );
  res.sendBody(body);
          
  res.finish();
}).listen(8000);
