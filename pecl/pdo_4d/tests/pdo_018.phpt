--TEST--
PDO Common: serializing
--SKIPIF--
<?php # vim:ft=php
if (!extension_loaded('pdo')) die('skip');
if (!interface_exists('Serializable')) die('skip no Serializable interface');
//$dir = getenv('REDIR_TEST_DIR');
//if (false == $dir) die('skip no driver');
require_once $dir . 'pdo_test.inc';
PDOTest::skip();
?>
--FILE--
<?php
if (getenv('REDIR_TEST_DIR') === false) putenv('REDIR_TEST_DIR='.dirname(__FILE__) . '/../../pdo/tests/');
require_once getenv('REDIR_TEST_DIR') . 'pdo_test.inc';
$db = PDOTest::factory();

class TestBase implements Serializable
{
	public    $BasePub = 'Public';
	protected $BasePro = 'Protected';
	private   $BasePri = 'Private';
	
	function serialize()
	{
		$serialized = array();
		foreach($this as $prop => $val) {
			$serialized[$prop] = $val;
		}
		$serialized = serialize($serialized);
		echo __METHOD__ . "() = '$serialized'\n";
		return $serialized;
	}
	
	function unserialize($serialized)
	{
		echo __METHOD__ . "($serialized)\n";
		foreach(unserialize($serialized) as $prop => $val) {
			$this->$prop = '#'.$val;
		}
		return true;
	}
}

class TestDerived extends TestBase
{
	public    $BasePub    = 'DerivedPublic';
	protected $BasePro    = 'DerivdeProtected';
	public    $DerivedPub = 'Public';
	protected $DerivedPro = 'Protected';
	private   $DerivedPri = 'Private';

	function serialize()
	{
		echo __METHOD__ . "()\n";
		return TestBase::serialize();
	}
	
	function unserialize($serialized)
	{
		echo __METHOD__ . "()\n";
		return TestBase::unserialize($serialized);
	}
}

class TestLeaf extends TestDerived
{
}

$db->exec('CREATE TABLE classtypes(id varchar(20), name VARCHAR(20) )');
$db->exec('INSERT INTO classtypes VALUES(\'0\', \'stdClass\')'); 
$db->exec('INSERT INTO classtypes VALUES(\'1\', \'TestBase\')'); 
$db->exec('INSERT INTO classtypes VALUES(\'2\', \'TestDerived\')'); 
$db->exec('INSERT INTO classtypes VALUES(\'4\', NULL)'); 
$db->exec('CREATE TABLE test(id int NOT NULL PRIMARY KEY, classtype varchar(20), val VARCHAR(255))');

$db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);

var_dump($db->query('SELECT COUNT(*) FROM classtypes')->fetchColumn());
var_dump($db->query('SELECT id, name FROM classtypes ORDER by id')->fetchAll(PDO::FETCH_COLUMN|PDO::FETCH_UNIQUE));

$objs = array();
$objs[0] = new stdClass;
$objs[1] = new TestBase;
$objs[2] = new TestDerived;
$objs[3] = new TestLeaf;

$stmt = $db->prepare('SELECT id FROM classtypes WHERE name=:cname');
$stmt->bindParam(':cname', $cname);

$ctypes = array();

foreach($objs as $obj)
{
	$cname = get_class($obj);
	$ctype = NULL; /* set default for non stored class name */
	$stmt->execute();
	$stmt->bindColumn('id', $ctype);
	$stmt->fetch(PDO::FETCH_BOUND);
	$ctypes[$cname] = $ctype;
}

echo "===TYPES===\n";
var_dump($ctypes);

unset($stmt);

echo "===INSERT===\n";
$stmt = $db->prepare('INSERT INTO test VALUES(:id, :classtype, :val)');
$stmt->bindParam(':id', $idx, PDO::PARAM_INT);
$stmt->bindParam(':classtype', $ctype, PDO::PARAM_STR);
$stmt->bindParam(':val', $val, PDO::PARAM_STR);

foreach($objs as $idx => $obj)
{
	$ctype = $ctypes[get_class($obj)];
	if (is_null($ctype)) { 
	    $ctype = "'4'"; 
	} else {
	    $ctype = "'$ctype'"; 
	}
	if (method_exists($obj, 'serialize'))
	{
		$val = $obj->serialize();
	}
	else
	{
		$val = '';
	}
	$db->query("INSERT INTO test VALUES($idx, $ctype, '$val')");
//	$stmt->execute();	
}
//s	$db->query("UPDATE test SET classtype = NULL WHERE classtype = 4)");

unset($stmt);

echo "===DATA===\n";
var_dump($db->query('SELECT test.val FROM test')->fetchAll(PDO::FETCH_COLUMN));

echo "===FAILURE===\n";
try
{
//	$db->query('SELECT classtypes.name AS name, test.val AS val FROM test LEFT JOIN classtypes ON test.classtype=classtypes.id')->fetchAll(PDO::FETCH_CLASS|PDO::FETCH_CLASSTYPE|PDO::FETCH_SERIALIZE, 'TestLeaf', array());
	$db->query("SELECT classtypes.name AS name, test.val AS val FROM test, classtypes WHERE test.classtype='4'")->fetchAll(PDO::FETCH_CLASS|PDO::FETCH_CLASSTYPE|PDO::FETCH_SERIALIZE, 'TestLeaf', array());
}
catch (PDOException $e)
{
	echo 'Exception:';
	echo $e->getMessage()."\n";
}

echo "===COUNT===\n";
//var_dump($db->query('SELECT COUNT(*) FROM test LEFT JOIN classtypes ON test.classtype=classtypes.id WHERE (classtypes.id IS NULL OR classtypes.id > 0)')->fetchColumn());
var_dump($db->query('SELECT COUNT(*) FROM test where id > 0')->fetchColumn());

echo "===DATABASE===\n";
//$stmt = $db->prepare('SELECT classtypes.name AS name, test.val AS val FROM test LEFT JOIN classtypes ON test.classtype=classtypes.id WHERE (classtypes.id IS NULL OR classtypes.id > 0)');
$stmt = $db->prepare("SELECT classtypes.name AS name, test.val AS val FROM test,  classtypes WHERE test.classtype=classtypes.id and classtypes.id <> '0'");

$stmt->execute();
$database = $stmt->fetchAll(PDO::FETCH_ASSOC);
//var_dump($database);

echo "===FETCHCLASS===\n";
$stmt->execute();
var_dump($stmt->fetchAll(PDO::FETCH_CLASS|PDO::FETCH_CLASSTYPE|PDO::FETCH_SERIALIZE, 'TestLeaf'));


?>
--EXPECTF--
string(1) "4"
array(4) {
  [0]=>
  string(8) "stdClass"
  [1]=>
  string(8) "TestBase"
  [2]=>
  string(11) "TestDerived"
  [4]=>
  NULL
}
===TYPES===
array(4) {
  ["stdClass"]=>
  string(1) "0"
  ["TestBase"]=>
  string(1) "1"
  ["TestDerived"]=>
  string(1) "2"
  ["TestLeaf"]=>
  NULL
}
===INSERT===
TestBase::serialize() = 'a:3:{s:7:"BasePub";s:6:"Public";s:7:"BasePro";s:9:"Protected";s:7:"BasePri";s:7:"Private";}'
TestDerived::serialize()
TestBase::serialize() = 'a:5:{s:7:"BasePub";s:13:"DerivedPublic";s:7:"BasePro";s:16:"DerivdeProtected";s:10:"DerivedPub";s:6:"Public";s:10:"DerivedPro";s:9:"Protected";s:7:"BasePri";s:7:"Private";}'
TestDerived::serialize()
TestBase::serialize() = 'a:5:{s:7:"BasePub";s:13:"DerivedPublic";s:7:"BasePro";s:16:"DerivdeProtected";s:10:"DerivedPub";s:6:"Public";s:10:"DerivedPro";s:9:"Protected";s:7:"BasePri";s:7:"Private";}'
===DATA===
array(4) {
  [0]=>
  string(0) ""
  [1]=>
  string(91) "a:3:{s:7:"BasePub";s:6:"Public";s:7:"BasePro";s:9:"Protected";s:7:"BasePri";s:7:"Private";}"
  [2]=>
  string(172) "a:5:{s:7:"BasePub";s:13:"DerivedPublic";s:7:"BasePro";s:16:"DerivdeProtected";s:10:"DerivedPub";s:6:"Public";s:10:"DerivedPro";s:9:"Protected";s:7:"BasePri";s:7:"Private";}"
  [3]=>
  string(172) "a:5:{s:7:"BasePub";s:13:"DerivedPublic";s:7:"BasePro";s:16:"DerivdeProtected";s:10:"DerivedPub";s:6:"Public";s:10:"DerivedPro";s:9:"Protected";s:7:"BasePri";s:7:"Private";}"
}
===FAILURE===
Exception:SQLSTATE[HY000]: General error: cannot unserialize class
===COUNT===
string(1) "3"
===DATABASE===
===FETCHCLASS===
TestBase::unserialize(a:3:{s:7:"BasePub";s:6:"Public";s:7:"BasePro";s:9:"Protected";s:7:"BasePri";s:7:"Private";})
TestDerived::unserialize()
TestBase::unserialize(a:5:{s:7:"BasePub";s:13:"DerivedPublic";s:7:"BasePro";s:16:"DerivdeProtected";s:10:"DerivedPub";s:6:"Public";s:10:"DerivedPro";s:9:"Protected";s:7:"BasePri";s:7:"Private";})
TestDerived::unserialize()
TestBase::unserialize(a:5:{s:7:"BasePub";s:13:"DerivedPublic";s:7:"BasePro";s:16:"DerivdeProtected";s:10:"DerivedPub";s:6:"Public";s:10:"DerivedPro";s:9:"Protected";s:7:"BasePri";s:7:"Private";})
array(3) {
  [0]=>
  object(TestBase)#8 (3) {
    ["BasePub"]=>
    string(7) "#Public"
    ["BasePro:protected"]=>
    string(10) "#Protected"
    ["BasePri:private"]=>
    string(8) "#Private"
  }
  [1]=>
  object(TestDerived)#9 (6) {
    ["BasePub"]=>
    string(14) "#DerivedPublic"
    ["BasePro:protected"]=>
    string(17) "#DerivdeProtected"
    ["DerivedPub"]=>
    string(7) "#Public"
    ["DerivedPro:protected"]=>
    string(10) "#Protected"
    ["DerivedPri:private"]=>
    string(7) "Private"
    ["BasePri:private"]=>
    string(8) "#Private"
  }
  [2]=>
  object(TestLeaf)#10 (6) {
    ["BasePub"]=>
    string(14) "#DerivedPublic"
    ["BasePro:protected"]=>
    string(17) "#DerivdeProtected"
    ["DerivedPub"]=>
    string(7) "#Public"
    ["DerivedPro:protected"]=>
    string(10) "#Protected"
    ["DerivedPri:private"]=>
    string(7) "Private"
    ["BasePri:private"]=>
    string(8) "#Private"
  }
}