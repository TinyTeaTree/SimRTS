package main

import (
	"fmt"
	"sort"
	"sync"
)

type Room struct {
	ID        string   `json:"id"`
	PlayerIDs []string `json:"player_ids"`
}

type RoomStore struct {
	mu    sync.Mutex
	rooms map[string]*Room
}

func NewRoomStore() *RoomStore {
	return &RoomStore{rooms: make(map[string]*Room)}
}

func (s *RoomStore) List() []Room {
	s.mu.Lock()
	defer s.mu.Unlock()

	out := make([]Room, 0, len(s.rooms))
	for _, room := range s.rooms {
		out = append(out, cloneRoom(room))
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out
}

func (s *RoomStore) Create(id string) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if _, exists := s.rooms[id]; exists {
		return Room{}, fmt.Errorf("room already exists")
	}
	room := &Room{ID: id, PlayerIDs: []string{}}
	s.rooms[id] = room
	return cloneRoom(room), nil
}

func (s *RoomStore) Join(roomID, playerID string) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[roomID]
	if !ok {
		return Room{}, fmt.Errorf("room not found")
	}
	if contains(room.PlayerIDs, playerID) {
		return cloneRoom(room), nil
	}
	room.PlayerIDs = append(room.PlayerIDs, playerID)
	sort.Strings(room.PlayerIDs)
	return cloneRoom(room), nil
}

func (s *RoomStore) Leave(roomID, playerID string) (Room, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	room, ok := s.rooms[roomID]
	if !ok {
		return Room{}, fmt.Errorf("room not found")
	}
	next := make([]string, 0, len(room.PlayerIDs))
	found := false
	for _, id := range room.PlayerIDs {
		if id == playerID {
			found = true
			continue
		}
		next = append(next, id)
	}
	if !found {
		return Room{}, fmt.Errorf("player not in room")
	}
	room.PlayerIDs = next
	return cloneRoom(room), nil
}

func cloneRoom(room *Room) Room {
	players := make([]string, len(room.PlayerIDs))
	copy(players, room.PlayerIDs)
	return Room{ID: room.ID, PlayerIDs: players}
}

func contains(ids []string, want string) bool {
	for _, id := range ids {
		if id == want {
			return true
		}
	}
	return false
}
