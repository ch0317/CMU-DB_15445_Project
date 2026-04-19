WITH player_event AS (
                       SELECT
                           a.teamID,
                           a.yearID,
                           a.lgID
                       FROM appearances a
                       JOIN awardsplayers ap
                         ON a.playerID = ap.playerID
                        AND a.yearID = ap.yearID
                        AND a.lgID = ap.lgID
                       GROUP BY
                           a.teamID,
                           a.yearID,
                           a.lgID
                       HAVING COUNT(DISTINCT a.playerID) > 5
                   ),

                   event_E AS (
                       SELECT pe.*
                       FROM player_event pe
                       WHERE EXISTS (
                           SELECT 1
                           FROM awardsmanagers am
                           WHERE am.yearID = pe.yearID
                             AND am.lgID = pe.lgID
                       )
                   )

                   SELECT
                       l.league,
                       t.name AS team_name,
                       COUNT(DISTINCT e.yearID) AS distinct_years
                   FROM event_E e
                   JOIN teams t
                     ON e.teamID = t.teamID
                    AND e.lgID = t.lgID
                   JOIN leagues l
                     ON e.lgID = l.lgID
                   WHERE l.active = 'Y'
                   GROUP BY
                       l.league,
                       t.name
                   HAVING COUNT(DISTINCT e.yearID) > 1
                   ORDER BY
                       distinct_years DESC,
                       team_name ASC;
