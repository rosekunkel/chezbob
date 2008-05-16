import servio
import pyui
import unionui
import sodauser
import time

bus = servio.ServIO("TestNugget", "5")
ui = pyui.PyUI(bus)
#ui = unionui.UnionUI(bus)

user = sodauser.SodaUser(login="jcm", balance=100, servio=bus, ui=ui, timeout=30)

ui.logIn(user)
time.sleep(100)
user.resetTTL()
#user.setBalance(user.getBalance() - 50)
#ui.vendComplete(user, "Test Item", "50")
#time.sleep(2)
#user.setBalance(user.getBalance() - 50)
#ui.vendComplete(user, "Test Item", "50")
#time.sleep(2)
#user.setBalance(1000)
#time.sleep(2)
ui.fpRead(user, 3)
time.sleep(2)
ui.fpRead(user, 2)
time.sleep(2)
ui.fpRead(user, 1)
time.sleep(2)
ui.fpLearnFail(user, 3, "didn't match")
time.sleep(2)
ui.fpLearnSuccess(user, "matched!")
time.sleep(2)
ui.logOut(user)
