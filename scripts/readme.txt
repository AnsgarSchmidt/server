** Status und Status�nderungen

* USERS.STATUS

NEW: Initialer Status, warten auf Best�tigung der Anmeldung.
�berg�nge: 
-> TUTORIAL, wenn Anmeldung von Benutzer best�tigt wurde.
-> INVALID, BANNED (nur manuell)

TUTORIAL: Emailadresse des Spielers ist korrekt, seine Anmeldung wurde
best�tigt, und er muss ein Tutorial bestehen.
�berg�nge:
-> ACTIVE, wenn er ein Tutorial abgeschlossen hat
-> INVALID, BANNED (nur manuell)

ACTIVE: Spieler hat das Tutorial erf�llt, und kann sich f�r Partien anmelden
�berg�nge:
-> INVALID, BANNED (nur manuell)

INVALID: Spieler hat ung�ltige Daten �bermittelt

BANNED: Spieler ist aus dem Spiel ausgeschlossen worden.


* SUBSCRIPTIONS.STATUS

WAITING: Warten auf Best�tigung
-> EXPIRED
-> CONFIRMED

CONFIRMED: Best�tigung eingetroffen
-> WAITING
-> ACTIVE

ACTIVE: Spiel ist gestartet
-> DEAD
