package main

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestLoginAndAuthedRooms(t *testing.T) {
	handler := newMux(NewSessionStore(), NewRoomStore())

	unauth := httptest.NewRecorder()
	handler.ServeHTTP(unauth, httptest.NewRequest(http.MethodGet, "/GetRooms", nil))
	if unauth.Code != http.StatusUnauthorized {
		t.Fatalf("GetRooms without token: %d", unauth.Code)
	}

	loginBody, _ := json.Marshal(map[string]string{"username": "alice"})
	loginRec := httptest.NewRecorder()
	loginReq := httptest.NewRequest(http.MethodPost, "/Login", bytes.NewReader(loginBody))
	loginReq.Header.Set("Content-Type", "application/json")
	handler.ServeHTTP(loginRec, loginReq)
	if loginRec.Code != http.StatusOK {
		t.Fatalf("Login: %d %s", loginRec.Code, loginRec.Body.String())
	}

	var session Session
	if err := json.Unmarshal(loginRec.Body.Bytes(), &session); err != nil {
		t.Fatalf("login json: %v", err)
	}
	if session.Token == "" || session.PlayerID == "" || session.Nickname != "alice" {
		t.Fatalf("session = %#v", session)
	}

	roomsRec := httptest.NewRecorder()
	roomsReq := httptest.NewRequest(http.MethodGet, "/GetRooms", nil)
	roomsReq.Header.Set(sessionTokenHeader, session.Token)
	handler.ServeHTTP(roomsRec, roomsReq)
	if roomsRec.Code != http.StatusOK {
		t.Fatalf("GetRooms with token: %d %s", roomsRec.Code, roomsRec.Body.String())
	}

	createBody, _ := json.Marshal(map[string]string{"id": "alpha"})
	createRec := httptest.NewRecorder()
	createReq := httptest.NewRequest(http.MethodPost, "/CreateRoom", bytes.NewReader(createBody))
	createReq.Header.Set("Content-Type", "application/json")
	createReq.Header.Set(sessionTokenHeader, session.Token)
	handler.ServeHTTP(createRec, createReq)
	if createRec.Code != http.StatusCreated {
		t.Fatalf("CreateRoom: %d %s", createRec.Code, createRec.Body.String())
	}

	joinRec := httptest.NewRecorder()
	joinReq := httptest.NewRequest(http.MethodPost, "/JoinRoom", bytes.NewReader(createBody))
	joinReq.Header.Set("Content-Type", "application/json")
	joinReq.Header.Set(sessionTokenHeader, session.Token)
	handler.ServeHTTP(joinRec, joinReq)
	if joinRec.Code != http.StatusOK {
		t.Fatalf("JoinRoom: %d %s", joinRec.Code, joinRec.Body.String())
	}

	var room Room
	if err := json.Unmarshal(joinRec.Body.Bytes(), &room); err != nil {
		t.Fatalf("join json: %v", err)
	}
	if len(room.PlayerIDs) != 0 {
		t.Fatalf("HTTP JoinRoom must not seat the player yet, got %#v", room.PlayerIDs)
	}
}

func TestLoginRejectsBadUsername(t *testing.T) {
	handler := newMux(NewSessionStore(), NewRoomStore())
	body, _ := json.Marshal(map[string]string{"username": "bad name"})
	rec := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodPost, "/Login", bytes.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	handler.ServeHTTP(rec, req)
	if rec.Code != http.StatusBadRequest {
		t.Fatalf("got %d", rec.Code)
	}
}

func TestStartRoomRequiresSeat(t *testing.T) {
	sessions := NewSessionStore()
	rooms := NewRoomStore()
	handler := newMux(sessions, rooms)

	loginBody, _ := json.Marshal(map[string]string{"username": "alice"})
	loginRec := httptest.NewRecorder()
	loginReq := httptest.NewRequest(http.MethodPost, "/Login", bytes.NewReader(loginBody))
	loginReq.Header.Set("Content-Type", "application/json")
	handler.ServeHTTP(loginRec, loginReq)
	var session Session
	if err := json.Unmarshal(loginRec.Body.Bytes(), &session); err != nil {
		t.Fatal(err)
	}

	createBody, _ := json.Marshal(map[string]string{"id": "alpha"})
	createRec := httptest.NewRecorder()
	createReq := httptest.NewRequest(http.MethodPost, "/CreateRoom", bytes.NewReader(createBody))
	createReq.Header.Set("Content-Type", "application/json")
	createReq.Header.Set(sessionTokenHeader, session.Token)
	handler.ServeHTTP(createRec, createReq)

	startRec := httptest.NewRecorder()
	startReq := httptest.NewRequest(http.MethodPost, "/StartRoom", bytes.NewReader(createBody))
	startReq.Header.Set("Content-Type", "application/json")
	startReq.Header.Set(sessionTokenHeader, session.Token)
	handler.ServeHTTP(startRec, startReq)
	if startRec.Code != http.StatusNotFound {
		t.Fatalf("StartRoom unseated: %d %s", startRec.Code, startRec.Body.String())
	}

	if _, _, err := rooms.Seat("alpha", session.PlayerID, nil); err != nil {
		t.Fatal(err)
	}
	startRec2 := httptest.NewRecorder()
	startReq2 := httptest.NewRequest(http.MethodPost, "/StartRoom", bytes.NewReader(createBody))
	startReq2.Header.Set("Content-Type", "application/json")
	startReq2.Header.Set(sessionTokenHeader, session.Token)
	handler.ServeHTTP(startRec2, startReq2)
	if startRec2.Code != http.StatusOK {
		t.Fatalf("StartRoom seated: %d %s", startRec2.Code, startRec2.Body.String())
	}
	var started Room
	if err := json.Unmarshal(startRec2.Body.Bytes(), &started); err != nil {
		t.Fatal(err)
	}
	if len(started.PlayerIDs) != 1 || started.PlayerIDs[0] != session.PlayerID {
		t.Fatalf("start roster %#v", started.PlayerIDs)
	}
	if len(started.Seats) != 1 || started.Seats[0] != 0 {
		t.Fatalf("start seats %#v", started.Seats)
	}
}
