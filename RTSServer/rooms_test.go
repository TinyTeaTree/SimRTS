package main

import (
	"testing"
	"time"
)

func TestRoomLifecycle(t *testing.T) {
	store := NewRoomStore()

	if _, err := store.Create("alpha"); err != nil {
		t.Fatalf("create: %v", err)
	}
	if _, err := store.Create("alpha"); err == nil {
		t.Fatal("expected duplicate create to fail")
	}

	room, err := store.Seat("alpha", "p2", nil)
	if err != nil {
		t.Fatalf("join p2: %v", err)
	}
	room, err = store.Seat("alpha", "p1", nil)
	if err != nil {
		t.Fatalf("join p1: %v", err)
	}
	if len(room.PlayerIDs) != 2 || room.PlayerIDs[0] != "p1" || room.PlayerIDs[1] != "p2" {
		t.Fatalf("players = %#v", room.PlayerIDs)
	}

	again, err := store.Seat("alpha", "p1", nil)
	if err != nil {
		t.Fatalf("idempotent join: %v", err)
	}
	if len(again.PlayerIDs) != 2 {
		t.Fatalf("duplicate join changed roster: %#v", again.PlayerIDs)
	}

	left, err := store.Leave("alpha", "p1")
	if err != nil {
		t.Fatalf("leave: %v", err)
	}
	if len(left.PlayerIDs) != 1 || left.PlayerIDs[0] != "p2" {
		t.Fatalf("after leave: %#v", left.PlayerIDs)
	}

	rooms := store.List()
	if len(rooms) != 1 || rooms[0].ID != "alpha" {
		t.Fatalf("list = %#v", rooms)
	}
}

func TestMarkStartRequiresSeatAndKickoff(t *testing.T) {
	store := NewRoomStore()
	if _, err := store.Create("alpha"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.MarkStart("alpha", "p1"); err == nil {
		t.Fatal("start without seat should fail")
	}
	if _, err := store.Seat("alpha", "p1", nil); err != nil {
		t.Fatal(err)
	}
	if _, err := store.MarkStart("alpha", "p1"); err != nil {
		t.Fatalf("start seated: %v", err)
	}
	jobs := store.KickoffBroadcasts(time.Now())
	if len(jobs) != 1 || jobs[0].KickoffID != 1 || jobs[0].RemainingMs == 0 {
		t.Fatalf("kickoff jobs %#v", jobs)
	}
	again, err := store.MarkStart("alpha", "p1")
	if err != nil {
		t.Fatal(err)
	}
	if len(again.PlayerIDs) != 1 {
		t.Fatalf("idempotent start %#v", again)
	}
}

func TestMarkStartWaitsForAllSeated(t *testing.T) {
	store := NewRoomStore()
	if _, err := store.Create("alpha"); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Seat("alpha", "p1", nil); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Seat("alpha", "p2", nil); err != nil {
		t.Fatal(err)
	}
	if _, err := store.MarkStart("alpha", "p1"); err != nil {
		t.Fatal(err)
	}
	if jobs := store.KickoffBroadcasts(time.Now()); len(jobs) != 0 {
		t.Fatalf("kickoff before all ready: %#v", jobs)
	}
	if _, err := store.MarkStart("alpha", "p2"); err != nil {
		t.Fatal(err)
	}
	if jobs := store.KickoffBroadcasts(time.Now()); len(jobs) != 1 {
		t.Fatalf("expected kickoff after both ready, got %#v", jobs)
	}
}
