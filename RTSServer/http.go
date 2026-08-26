package main

import (
	"encoding/json"
	"errors"
	"io"
	"log"
	"net/http"
	"regexp"
	"strings"
	"time"
)

const (
	maxBodyBytes       = 1 << 16
	sessionTokenHeader = "X-Session-Token"
)

var idPattern = regexp.MustCompile(`^[A-Za-z0-9_-]{1,64}$`)

type usernameBody struct {
	Username string `json:"username"`
}

type roomIDBody struct {
	ID string `json:"id"`
}

type errorBody struct {
	Error string `json:"error"`
}

func newMux(sessions *SessionStore, rooms *RoomStore) http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("POST /Login", func(w http.ResponseWriter, r *http.Request) {
		var body usernameBody
		if err := decodeJSON(r, &body); err != nil {
			writeError(w, http.StatusBadRequest, err.Error())
			return
		}
		nickname, err := normalizeID(body.Username, "username")
		if err != nil {
			writeError(w, http.StatusBadRequest, err.Error())
			return
		}
		session, err := sessions.Login(nickname)
		if err != nil {
			writeError(w, http.StatusInternalServerError, "login failed")
			return
		}
		writeJSON(w, http.StatusOK, session)
	})

	mux.HandleFunc("GET /GetRooms", requireSession(sessions, func(w http.ResponseWriter, r *http.Request, _ Session) {
		writeJSON(w, http.StatusOK, map[string]any{"rooms": rooms.List()})
	}))
	mux.HandleFunc("POST /CreateRoom", requireSession(sessions, func(w http.ResponseWriter, r *http.Request, _ Session) {
		var body roomIDBody
		if err := decodeJSON(r, &body); err != nil {
			writeError(w, http.StatusBadRequest, err.Error())
			return
		}
		id, err := normalizeID(body.ID, "id")
		if err != nil {
			writeError(w, http.StatusBadRequest, err.Error())
			return
		}
		room, err := rooms.Create(id)
		if err != nil {
			writeError(w, http.StatusConflict, err.Error())
			return
		}
		writeJSON(w, http.StatusCreated, room)
	}))
	mux.HandleFunc("POST /JoinRoom", requireSession(sessions, func(w http.ResponseWriter, r *http.Request, _ Session) {
		handleRoomGet(w, r, rooms)
	}))
	mux.HandleFunc("POST /LeaveRoom", requireSession(sessions, func(w http.ResponseWriter, r *http.Request, session Session) {
		handleRoomPlayer(w, r, session.PlayerID, rooms.Leave)
	}))
	mux.HandleFunc("POST /StartRoom", requireSession(sessions, func(w http.ResponseWriter, r *http.Request, session Session) {
		handleRoomPlayer(w, r, session.PlayerID, rooms.MarkStart)
	}))
	return withLogs(mux)
}

func requireSession(sessions *SessionStore, next func(http.ResponseWriter, *http.Request, Session)) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		token := strings.TrimSpace(r.Header.Get(sessionTokenHeader))
		if token == "" {
			writeError(w, http.StatusUnauthorized, "missing "+sessionTokenHeader+" header")
			return
		}
		session, ok := sessions.Lookup(token)
		if !ok {
			writeError(w, http.StatusUnauthorized, "invalid session token")
			return
		}
		next(w, r, session)
	}
}

func handleRoomGet(w http.ResponseWriter, r *http.Request, rooms *RoomStore) {
	var body roomIDBody
	if err := decodeJSON(r, &body); err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	roomID, err := normalizeID(body.ID, "id")
	if err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	room, err := rooms.Get(roomID)
	if err != nil {
		writeError(w, http.StatusNotFound, err.Error())
		return
	}
	writeJSON(w, http.StatusOK, room)
}

func handleRoomPlayer(w http.ResponseWriter, r *http.Request, playerID string, fn func(roomID, playerID string) (Room, error)) {
	var body roomIDBody
	if err := decodeJSON(r, &body); err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	roomID, err := normalizeID(body.ID, "id")
	if err != nil {
		writeError(w, http.StatusBadRequest, err.Error())
		return
	}
	room, err := fn(roomID, playerID)
	if err != nil {
		switch err.Error() {
		case "room not found":
			writeError(w, http.StatusNotFound, err.Error())
		case "player not in room":
			writeError(w, http.StatusNotFound, err.Error())
		default:
			writeError(w, http.StatusBadRequest, err.Error())
		}
		return
	}
	writeJSON(w, http.StatusOK, room)
}

func decodeJSON(r *http.Request, dest any) error {
	r.Body = http.MaxBytesReader(nil, r.Body, maxBodyBytes)
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(dest); err != nil {
		if errors.Is(err, io.EOF) {
			return errors.New("empty body")
		}
		return err
	}
	if decoder.More() {
		return errors.New("unexpected extra JSON")
	}
	return nil
}

func normalizeID(value, field string) (string, error) {
	id := strings.TrimSpace(value)
	if !idPattern.MatchString(id) {
		return "", errors.New(field + " must be 1-64 letters, digits, _ or -")
	}
	return id, nil
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(value); err != nil {
		log.Printf("write json: %v", err)
	}
}

func writeError(w http.ResponseWriter, status int, message string) {
	writeJSON(w, status, errorBody{Error: message})
}

type statusWriter struct {
	http.ResponseWriter
	status int
}

func (w *statusWriter) WriteHeader(status int) {
	w.status = status
	w.ResponseWriter.WriteHeader(status)
}

func withLogs(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		started := time.Now()
		sw := &statusWriter{ResponseWriter: w, status: http.StatusOK}
		next.ServeHTTP(sw, r)
		log.Printf("%s %s -> %d (%s)", r.Method, r.URL.Path, sw.status, time.Since(started).Round(time.Millisecond))
	})
}
