SELECT hof_player.name AS hof_player_name,
       teammate.name AS earliest_teammate_name,
       teammate.yearID AS earliest_teammate_year
FROM (
    -- Get inducted players into hall of fame
    SELECT p.playerID, p.nameFirst || ' (' || p.nameGiven || ') ' || p.nameLast AS name
    FROM halloffame h
    JOIN people p ON h.playerID = p.playerID
    WHERE h.inducted = 'Y'
) AS hof_player
-- For each HOF player
JOIN LATERAL (
    SELECT a2.yearID, p2.playerID,
	  p2.nameFirst || ' (' || p2.nameGiven || ') ' || p2.nameLast AS name
    FROM appearances a1, appearances a2, people p2
      -- Find appearances a1 where the HOF player appeared
    WHERE a1.playerID = hof_player.playerID
      -- Find all appearances a2 of other players that overlap with HOF player
     AND a1.yearID = a2.yearID
     AND a1.teamID = a2.teamID
     AND a1.playerID <> a2.playerID
     AND a2.playerID = p2.playerID
    ORDER BY a2.yearID, name
    LIMIT 1
) AS teammate ON TRUE
ORDER BY hof_player.name, teammate.yearID
LIMIT 10;
