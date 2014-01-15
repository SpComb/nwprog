#!/usr/bin/env python

import BaseHTTPServer

class Handler (BaseHTTPServer.BaseHTTPRequestHandler) :
	def do_GET (self) :
		self.send_response(200, "OK")
		self.send_header("X-Test", "Testing")
		self.end_headers()
		self.wfile.write("Data\r\n")

def main (listen='localhost', port=8080) :
	server = BaseHTTPServer.HTTPServer((listen, port), Handler)
	server.serve_forever()

if __name__ == '__main__' :
	main()
