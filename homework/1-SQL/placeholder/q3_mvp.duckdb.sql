WITH Gold_Glove_player AS (
    SELECT ap.playerID, ap.yearID, ap.lgID
    FROM awardsplayers ap
    JOIN leagues l 
      ON ap.lgID = l.lgID
    WHERE 
        ap.awardID = 'Gold Glove'
        AND ap.yearID > 1999 
        AND l.active = 'Y'
),

team_avg AS (
    SELECT teamID, AVG(G_batting) AS g_av
    FROM appearances
    WHERE yearID > 1999
    GROUP BY teamID
),

qualified AS (
    SELECT 
        a.playerID,
        a.teamID,
        a.yearID
    FROM appearances a
    JOIN Gold_Glove_player gp
      ON a.playerID = gp.playerID
     AND a.yearID = gp.yearID
     AND a.lgID = gp.lgID
    JOIN team_avg ta
      ON a.teamID = ta.teamID
    WHERE 
        a.G_batting > ta.g_av
)

SELECT 
    p.nameGiven,
    q.teamID,
    COUNT(DISTINCT q.yearID) AS distinct_years
FROM qualified q
JOIN people p
  ON q.playerID = p.playerID
GROUP BY 
    p.nameGiven, q.teamID
ORDER BY 
    distinct_years DESC,
    p.nameGiven ASC
LIMIT 10;
