select people.nameGiven, teamID, count(distinct awardsplayers.yearID) as distinct_years
 from people, awardsplayers, leagues, appearances
where
      -- Find awards given during active leagues after 1999
      awardsplayers.lgID = leagues.lgID and leagues.active = 'Y'
  and awardsplayers.yearID > 1999 
  and people.playerID = awardsplayers.playerID 
  and appearances.yearID = awardsplayers.yearID
  and appearances.lgID = leagues.lgID
  and appearances.playerID = awardsplayers.playerID
      -- Check G_batting is greater than average batting of all team player appearances after 1999
  and G_batting > (SELECT AVG(G_batting) FROM appearances a2 WHERE a2.yearID > 1999 and a2.teamID = appearances.teamID)
      -- Filter based on awardID
  and awardID IN ('Gold Glove')
group by nameGiven, teamID
order by distinct_years desc, people.nameGiven
limit 10
;
