package main

import (
	"flag"
	"log"
	"net"
	"net/http"
	"strconv"
)

func main() {
	bind := flag.String("bind", "127.0.0.1", "address to listen on")
	port := flag.Int("port", 8080, "HTTP port")
	udpPort := flag.Int("udp-port", 8081, "UDP relay port")
	flag.Parse()

	sessions := NewSessionStore()
	rooms := NewRoomStore()

	go serveUDP(*bind, *udpPort, sessions, rooms)

	addr := net.JoinHostPort(*bind, strconv.Itoa(*port))
	server := &http.Server{
		Addr:    addr,
		Handler: newMux(sessions, rooms),
	}

	log.Printf("RTSServer matchmaking on http://%s", addr)
	log.Printf("POST /Login       {\"username\":\"alice\"}  (no token)")
	log.Printf("Header            %s: <session_token>", sessionTokenHeader)
	log.Printf("GET  /GetRooms")
	log.Printf("POST /CreateRoom  {\"id\":\"room\"}")
	log.Printf("POST /JoinRoom    {\"id\":\"room\"}  (authorize only; UDP Hello seats)")
	log.Printf("POST /LeaveRoom   {\"id\":\"room\"}")
	log.Printf("POST /StartRoom   {\"id\":\"room\"}  (ready; seated players only)")

	if err := server.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatal(err)
	}
}
