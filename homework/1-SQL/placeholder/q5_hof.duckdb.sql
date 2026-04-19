WITH hof AS (
    SELECT playerID
    FROM halloffame
    WHERE inducted = 'Y'
),

teammates AS (
    SELECT 
        h.playerID AS hof_player,
        a2.playerID AS teammate,
        a1.yearID
    FROM hof h
    JOIN appearances a1
      ON h.playerID = a1.playerID
    JOIN appearances a2
      ON a1.teamID = a2.teamID
     AND a1.yearID = a2.yearID
     AND a1.playerID <> a2.playerID
),

earliest_year AS (
    SELECT 
        hof_player,
        MIN(yearID) AS first_year
    FROM teammates
    GROUP BY hof_player
),

earliest_teammates AS (
    SELECT 
        t.hof_player,
        t.teammate,
        t.yearID
    FROM teammates t
    JOIN earliest_year e
      ON t.hof_player = e.hof_player
     AND t.yearID = e.first_year
)

SELECT 
    p1.nameFirst || ' (' || p1.nameGiven || ') ' || p1.nameLast AS player_name,
    MIN(p2.nameFirst || ' (' || p2.nameGiven || ') ' || p2.nameLast) AS teammate_name,yearID 
FROM earliest_teammates et
JOIN people p1
  ON et.hof_player = p1.playerID
JOIN people p2
  ON et.teammate = p2.playerID
GROUP BY player_name,yearID
ORDER BY player_name ASC
LIMIT 10;
