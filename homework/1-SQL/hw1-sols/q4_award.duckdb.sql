with aplayers as (
  -- find all triplets (leagueID, teamID, yearID)
  -- where the team from an active league has at least 5 players who won awards
  select leagues.lgID, teamID, awardsplayers.yearID
  from awardsplayers, leagues, appearances
  where awardsplayers.lgID = leagues.lgID and leagues.active = 'Y'
    and appearances.yearID = awardsplayers.yearID
    and appearances.lgID = leagues.lgID
    and appearances.playerID = awardsplayers.playerID
  group by leagues.lgID, teamID, awardsplayers.yearID
  having count(distinct awardsplayers.playerID) > 5
),
amanagers as (
  -- find all unique triplets (leagueID, teamID, yearID)
  -- where the team from an active league has a manager who won an award
  select distinct leagues.lgID, teamID, awardsmanagers.yearID
  from awardsmanagers, leagues, managers
  where awardsmanagers.lgID = leagues.lgID and leagues.active = 'Y'
    and managers.playerID = awardsmanagers.playerID
    and managers.yearID = awardsmanagers.yearID 
)

select league, name as team_name, count(distinct teams.yearID) as distinct_years
from leagues, teams, aplayers
where leagues.lgID = teams.lgID 
  -- First join gets (league, team, year) with at least 5 players who won awards
  and teams.teamID = aplayers.teamID
  and teams.yearID = aplayers.yearID
  and teams.lgID = aplayers.lgID
  -- Then we check in that same year, if the team manager has an award
  and exists (
	SELECT 1 FROM amanagers
         WHERE amanagers.lgID = teams.lgID and amanagers.teamID = teams.teamID and amanagers.yearID = teams.yearID
  )

-- Limit to the teams who have had the above event happen more than once.
group by league, name
having count(distinct teams.yearID) > 1
order by distinct_years desc, name
;
