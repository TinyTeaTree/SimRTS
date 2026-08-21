package main

import (
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"sync"
)

type Session struct {
	Token    string `json:"session_token"`
	PlayerID string `json:"player_id"`
	Nickname string `json:"nickname"`
}

type SessionStore struct {
	mu       sync.Mutex
	sessions map[string]Session
}

func NewSessionStore() *SessionStore {
	return &SessionStore{sessions: make(map[string]Session)}
}

func (s *SessionStore) Login(nickname string) (Session, error) {
	token, err := randomHex(32)
	if err != nil {
		return Session{}, fmt.Errorf("session token: %w", err)
	}
	playerID, err := randomHex(8)
	if err != nil {
		return Session{}, fmt.Errorf("player id: %w", err)
	}
	session := Session{
		Token:    token,
		PlayerID: "p_" + playerID,
		Nickname: nickname,
	}

	s.mu.Lock()
	defer s.mu.Unlock()
	s.sessions[token] = session
	return session, nil
}

func (s *SessionStore) Lookup(token string) (Session, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	session, ok := s.sessions[token]
	return session, ok
}

func randomHex(n int) (string, error) {
	buf := make([]byte, n)
	if _, err := rand.Read(buf); err != nil {
		return "", err
	}
	return hex.EncodeToString(buf), nil
}
