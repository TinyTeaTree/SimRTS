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
